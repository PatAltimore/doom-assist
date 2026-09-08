//
// net_gui.c -- doom-assist patch: headless reimplementation of
// NET_WaitForLaunch (declared in net_gui.h, called once from d_loop.c's
// D_InitNetGame right after a client finishes connecting to a server).
//
// What this normally is: real chocolate-doom's net_gui.c draws a
// text-console "lobby" (using its own TXT_* widget toolkit) listing
// connected players, with a "Start game" button the *controller* (the
// player who started the server) clicks once everyone they're expecting
// has joined. Vendoring that toolkit just for this one screen made no
// sense here -- shell.html's own pre-boot Host/Join overlay already *is*
// that lobby, and by the time this function is even reached the player
// has already committed to Host or Join and D_ConnectNetGame is already
// under way. So the GUI itself is gone; what's left below is only the
// underlying protocol logic real net_gui.c's NET_WaitForLaunch actually
// depended on, reimplemented directly against net_client.c/net_server.c
// (both vendored verbatim, unlike this file):
//
//   - Keep pumping the connection (NET_CL_Run/NET_SV_Run) until the
//     server actually signals "go" (net_waiting_for_launch flips false).
//   - Decide WHEN to send that signal, for whoever is the controller.
//     Real chocolate-doom offers two ways: a human clicking the GUI's
//     "Start game" button, or "-nodes N" auto-launching once N total
//     players have joined (CheckAutoLaunch below, ported near-verbatim
//     from upstream's net_gui.c). This project only uses the latter --
//     shell.html's Host flow always passes "-nodes 2" (Phase 1 is
//     strictly 2 players), so the game auto-launches the instant the
//     second player's connection is registered, with no separate
//     "start" step for the host to remember to click.
//

#include <stdlib.h>

#include <emscripten.h>

#include "doomtype.h"
#include "i_system.h"
#include "m_argv.h"
#include "net_client.h"
#include "net_gui.h"
#include "net_server.h"

// Set by -nodes <n> below; 0 means "never auto-launch" (matches upstream's
// default when the flag is absent -- harmless here since Phase 1 always
// passes it, but kept for whenever a real host-configurable lobby exists).
static int expected_nodes = 0;

static void ParseCommandLineArgs(void)
{
    int i;

    //!
    // @arg <n>
    // @category net
    //
    // Autostart the netgame when n nodes (clients) have joined the server.
    // doom-assist's shell.html always passes this for a hosted game --
    // see the file-level comment above for why there's no manual
    // "Start game" button to press instead.
    //

    i = M_CheckParmWithArgs("-nodes", 1);
    if (i > 0)
    {
        expected_nodes = atoi(myargv[i + 1]);
    }
}

// Ported from upstream net_gui.c's CheckAutoLaunch, minus the GUI state it
// also updated -- the launch decision itself is unchanged.
static void CheckAutoLaunch(void)
{
    int nodes;

    if (net_client_received_wait_data
     && net_client_wait_data.is_controller
     && expected_nodes > 0)
    {
        nodes = net_client_wait_data.num_players
              + net_client_wait_data.num_drones;

        if (nodes >= expected_nodes)
        {
            NET_CL_LaunchGame();
            expected_nodes = 0; // only ever auto-launch once
        }
    }
}

void NET_WaitForLaunch(void)
{
    ParseCommandLineArgs();

    while (net_waiting_for_launch)
    {
        CheckAutoLaunch();

        NET_CL_Run();
        NET_SV_Run();

        if (!net_client_connected)
        {
            I_Error("Lost connection to server");
        }

        // This looks like it would freeze the tab -- a while loop with no
        // way out except a variable some *other* code path has to flip.
        // It doesn't, because -sASYNCIFY (build-emscripten.sh) rewrites
        // emscripten_sleep into a real yield back to the browser's event
        // loop and resumes this function right where it left off once the
        // timer fires. That's what lets the browser's WebSocket onmessage
        // handler keep running (delivering the other player's packets into
        // assist_net_ws_receive's queue) while this loop "blocks" -- the
        // same reason d_loop.c's own BlockUntilStart gets away with an
        // ordinary-looking I_Sleep loop right after this function returns.
        emscripten_sleep(100);
    }
}
