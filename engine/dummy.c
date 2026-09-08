/*
 * dummy.c
 *
 *  Created on: 16.02.2015
 *      Author: Florian
 */


/*---------------------------------------------------------------------*
 *  include files                                                      *
 *---------------------------------------------------------------------*/

#include "doomtype.h"
#include "net_defs.h"
// doom-assist patch: dummy.c's master-server/launch-wait stubs below are
// guarded by FEATURE_MULTIPLAYER, which is only *defined* by this header
// (doomfeatures.h) -- without including it here, the guard always reads as
// "not defined" (the preprocessor has never seen it) and the stubs quietly
// vanish, even though they still look present in this file. That's exactly
// what happened the first time: net_server.c/d_loop.c called them for
// real, but nothing in this translation unit ever defined them, so the
// final link step failed with "undefined symbol" for all three.
#include "doomfeatures.h"
// Pulls in the real prototypes for the three stubs below, so a typo in
// their signature here would be a compile error instead of a silent
// mismatch only the linker (or nothing at all) would ever catch.
#include "net_query.h"

/*---------------------------------------------------------------------*
 *  local definitions                                                  *
 *---------------------------------------------------------------------*/

/*---------------------------------------------------------------------*
 *  external declarations                                              *
 *---------------------------------------------------------------------*/

/*---------------------------------------------------------------------*
 *  public data                                                        *
 *---------------------------------------------------------------------*/

// doom-assist patch: net_client_connected/drone used to live here as
// permanent false stubs, back when FEATURE_MULTIPLAYER was compiled off
// and no net_*.c file defined them for real. Now that multiplayer is
// enabled (net_websockets.c/doomfeatures.h), net_client.c defines both
// of these itself -- keeping them here too would be a duplicate
// definition. See net_client.c:113/131 for the real ones.

/*---------------------------------------------------------------------*
 *  private data                                                       *
 *---------------------------------------------------------------------*/

/*---------------------------------------------------------------------*
 *  private functions                                                  *
 *---------------------------------------------------------------------*/

/*---------------------------------------------------------------------*
 *  public functions                                                   *
 *---------------------------------------------------------------------*/

#ifndef FEATURE_SOUND

void I_InitTimidityConfig(void)
{
}

#endif

#ifdef FEATURE_MULTIPLAYER
// doom-assist patch: master-server discovery stubs.
// net_server.c calls these two (net_query.c's real job: talk to
// doomworld.com's public server list over the network) unconditionally
// from its own per-tic housekeeping, regardless of whether a game ever
// asks to register with a master server. This build's rooms are private
// by design -- players share a room ID out of band, there's no public
// server browser -- so net_query.c's actual implementation was never
// vendored; these just report "no master server" so net_server.c's
// master-server bookkeeping quietly does nothing instead of failing to
// link.
net_addr_t *NET_Query_ResolveMaster(net_context_t *context)
{
    return NULL;
}

void NET_Query_AddToMaster(net_addr_t *master_addr)
{
}

// net_server.c's per-tic housekeeping also checks incoming packets against
// the master server address and, if one matches, hands it to this function
// to decode (real net_query.c parses the server-list reply here). Since
// NET_Query_ResolveMaster above always returns NULL, master_server in
// net_server.c is always NULL too, so the "packet came from the master"
// check that guards this call can itself never be true -- this stub exists
// only so the *call site* still links, not because it can ever run.
void NET_Query_MasterResponse(net_packet_t *packet)
{
}
#endif

/*---------------------------------------------------------------------*
 *  eof                                                                *
 *---------------------------------------------------------------------*/
