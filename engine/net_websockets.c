//
// net_websockets.c -- doom-assist patch: a net_module_t that talks over a
// browser WebSocket relay instead of a real socket.
//
// net_module_t (net_defs.h) is chocolate-doom's own abstraction over "how
// do packets actually get from one player to another" -- five functions
// (InitClient/InitServer/SendPacket/RecvPacket/AddrToString/FreeAddress/
// ResolveAddress) that net_client.c and net_server.c call without caring
// how they're implemented. Normally net_sdl.c fills that contract with a
// real UDP socket (SDLNet_UDP_Open/Send/Recv). Browsers can't open raw
// UDP sockets at all, so this file fills the exact same contract by
// bridging to a real WebSocket the JS side (shell.html) opens to Azure
// Web PubSub -- everything above this file (net_client.c, net_server.c,
// and the lockstep tic machinery in g_game.c/d_loop.c) is completely
// unaware packets are travelling over a relay instead of the open
// internet. That unawareness is the entire point of net_module_t existing
// as a swappable interface in the first place.
//
// Addressing: real chocolate-doom's net_sdl.c keys its address table by
// IP:port. There's no such thing here -- Azure Web PubSub's *group*
// feature delivers a message to every other browser that joined the same
// room, and tags each one with the sender's own Web PubSub connection ID
// (an opaque string Web PubSub assigns, not an IP). That connection-ID
// string is this module's "address": net_client.c/net_server.c only ever
// compare net_addr_t by pointer identity and hand it back to SendPacket,
// exactly the same contract net_sdl.c honors with real IP addresses --
// they never need to know it's a string instead.
//
// The two directions of the JS bridge are asymmetric, matching how the
// browser APIs on each side actually work:
//   - Outgoing (SendPacket): a synchronous call straight into JS via
//     EM_JS, same as this project's other JS bridges.
//   - Incoming: there's no socket to poll here, so nothing on the C side
//     can pull a packet on demand. Messages arrive whenever the
//     browser's WebSocket onmessage handler fires, which can happen at
//     any point between ticks -- so shell.html pushes each arrival into
//     C (assist_net_ws_receive, EMSCRIPTEN_KEEPALIVE below) into a small
//     queue, and RecvPacket just drains one entry per call, the same way
//     it always polled a socket for "is anything here yet".

#include <string.h>

#include <emscripten.h>

#include "doomtype.h"
#include "i_system.h"
#include "m_misc.h"
#include "net_defs.h"
#include "net_io.h"
#include "net_packet.h"
#include "net_websockets.h"
#include "z_zone.h"

// Matches net_sdl.c's own UDP MTU-ish assumption (SDLNet_AllocPacket(1500))
// -- DOOM's own ticcmd-batched packets never get close to this.
#define NET_WS_MAX_PACKET 1500

// -----------------------------------------------------------------------
// Address table -- "find or create a net_addr_t for this peer-id string",
// same shape as NET_SDL_FindAddress in net_sdl.c, just keyed by a Web
// PubSub connection-ID string instead of an IPaddress struct.
// -----------------------------------------------------------------------

#define NET_WS_MAX_PEER_ID 64

typedef struct
{
    net_addr_t net_addr;
    char peer_id[NET_WS_MAX_PEER_ID];
} addrpair_t;

static addrpair_t **addr_table;
static int addr_table_size = -1;

static void NET_WS_InitAddrTable(void)
{
    addr_table_size = 16;
    addr_table = Z_Malloc(sizeof(addrpair_t *) * addr_table_size, PU_STATIC, 0);
    memset(addr_table, 0, sizeof(addrpair_t *) * addr_table_size);
}

static net_addr_t *NET_WS_FindAddress(const char *peer_id)
{
    addrpair_t *new_entry;
    int empty_entry = -1;
    int i;

    if (addr_table_size < 0)
    {
        NET_WS_InitAddrTable();
    }

    for (i = 0; i < addr_table_size; ++i)
    {
        if (addr_table[i] != NULL && strcmp(addr_table[i]->peer_id, peer_id) == 0)
        {
            return &addr_table[i]->net_addr;
        }

        if (empty_entry < 0 && addr_table[i] == NULL)
            empty_entry = i;
    }

    // Not found -- add it, growing the table first if it's full (same
    // grow-by-doubling approach net_sdl.c uses).

    if (empty_entry < 0)
    {
        addrpair_t **new_addr_table;
        int new_addr_table_size = addr_table_size * 2;

        new_addr_table = Z_Malloc(sizeof(addrpair_t *) * new_addr_table_size,
                                   PU_STATIC, 0);
        memset(new_addr_table, 0, sizeof(addrpair_t *) * new_addr_table_size);
        memcpy(new_addr_table, addr_table,
               sizeof(addrpair_t *) * addr_table_size);
        Z_Free(addr_table);
        empty_entry = addr_table_size;
        addr_table = new_addr_table;
        addr_table_size = new_addr_table_size;
    }

    new_entry = Z_Malloc(sizeof(addrpair_t), PU_STATIC, 0);
    M_StringCopy(new_entry->peer_id, peer_id, sizeof(new_entry->peer_id));
    new_entry->net_addr.handle = new_entry->peer_id;
    new_entry->net_addr.module = &net_websockets_module;

    addr_table[empty_entry] = new_entry;

    return &new_entry->net_addr;
}

static void NET_WS_FreeAddress(net_addr_t *addr)
{
    int i;

    for (i = 0; i < addr_table_size; ++i)
    {
        if (addr_table[i] != NULL && addr == &addr_table[i]->net_addr)
        {
            Z_Free(addr_table[i]);
            addr_table[i] = NULL;
            return;
        }
    }

    I_Error("NET_WS_FreeAddress: Attempted to remove an unused address!");
}

// -----------------------------------------------------------------------
// Incoming packet queue -- see the file-level comment above for why this
// exists instead of RecvPacket polling something directly.
// -----------------------------------------------------------------------

#define NET_WS_QUEUE_SIZE 64

typedef struct
{
    char peer_id[NET_WS_MAX_PEER_ID];
    byte data[NET_WS_MAX_PACKET];
    int len;
} queued_packet_t;

static queued_packet_t recv_queue[NET_WS_QUEUE_SIZE];
static int queue_head = 0; // next slot assist_net_ws_receive will fill
static int queue_tail = 0; // next slot RecvPacket will drain

// Called from JS (shell.html) whenever a game-data message arrives over
// the WebSocket -- `from` is the sender's Web PubSub connection ID,
// `data`/`len` the raw packet bytes exactly as SendPacket below handed
// them to JS. Web PubSub delivers group-message payloads unmodified, so
// there's no framing of our own to undo here.
EMSCRIPTEN_KEEPALIVE void assist_net_ws_receive(const char *from,
                                                 const unsigned char *data,
                                                 int len)
{
    int next_head = (queue_head + 1) % NET_WS_QUEUE_SIZE;

    if (next_head == queue_tail)
        return; // queue full -- drop rather than clobber an unread packet

    if (len > NET_WS_MAX_PACKET)
        len = NET_WS_MAX_PACKET;

    M_StringCopy(recv_queue[queue_head].peer_id, from, NET_WS_MAX_PEER_ID);
    memcpy(recv_queue[queue_head].data, data, len);
    recv_queue[queue_head].len = len;
    queue_head = next_head;
}

// -----------------------------------------------------------------------
// net_module_t implementation
// -----------------------------------------------------------------------
// Both Init functions are trivial: unlike net_sdl.c, there's no socket
// for *this* file to open -- shell.html already owns the real WebSocket
// connection to Web PubSub by the time either of these could run (the
// player has to pick "Host"/"Join" in the browser UI before
// D_ConnectNetGame ever gets called), so there's nothing left to do here.

static boolean NET_WS_InitClient(void)
{
    return true;
}

static boolean NET_WS_InitServer(void)
{
    return true;
}

// JS-side glue that actually publishes onto the WebSocket shell.html
// opened. `to` is the destination peer's Web PubSub connection ID, or an
// empty string for net_broadcast_addr -- see shell.html's own Web PubSub
// wiring for how an empty `to` turns into an actual group broadcast
// versus a message addressed to one specific connection.
EM_JS(void, net_ws_js_send, (const char *to, const unsigned char *data, int len), {
    if (typeof assistNetWsSend === "function") {
        assistNetWsSend(UTF8ToString(to), HEAPU8.slice(data, data + len));
    }
});

static void NET_WS_SendPacket(net_addr_t *addr, net_packet_t *packet)
{
    const char *to;

    if (addr == &net_broadcast_addr)
        to = "";
    else
        to = (const char *) addr->handle;

    net_ws_js_send(to, packet->data, (int) packet->len);
}

static boolean NET_WS_RecvPacket(net_addr_t **addr, net_packet_t **packet)
{
    queued_packet_t *q;

    if (queue_tail == queue_head)
        return false; // nothing queued

    q = &recv_queue[queue_tail];
    queue_tail = (queue_tail + 1) % NET_WS_QUEUE_SIZE;

    *packet = NET_NewPacket(q->len);
    memcpy((*packet)->data, q->data, q->len);
    (*packet)->len = q->len;

    *addr = NET_WS_FindAddress(q->peer_id);

    return true;
}

static void NET_WS_AddrToString(net_addr_t *addr, char *buffer, int buffer_len)
{
    M_StringCopy(buffer, (const char *) addr->handle, buffer_len);
}

// There's no DNS/IP to resolve here -- the "address" a player connects
// to is just the host's Web PubSub connection ID (or an agreed room-level
// alias shell.html assigns the host, e.g. "host"), so this just treats
// the string as a peer-id directly, same as ResolveAddress's real job in
// net_sdl.c is "turn this string into a net_addr_t", just without a real
// network lookup in between.
static net_addr_t *NET_WS_ResolveAddress(char *address)
{
    return NET_WS_FindAddress(address);
}

// Complete module -- same seven function pointers net_sdl_module fills,
// see net_defs.h's net_module_t for the contract each one has to meet.
net_module_t net_websockets_module =
{
    NET_WS_InitClient,
    NET_WS_InitServer,
    NET_WS_SendPacket,
    NET_WS_RecvPacket,
    NET_WS_AddrToString,
    NET_WS_FreeAddress,
    NET_WS_ResolveAddress,
};
