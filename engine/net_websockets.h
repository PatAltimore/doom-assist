//
// net_websockets.h -- doom-assist patch: WebSocket-relay net_module_t.
//
// See net_websockets.c for the full explanation. This header exists only
// so d_loop.c can reference net_websockets_module the same way it already
// references net_sdl_module -- same shape as net_sdl.h.
//

#ifndef NET_WEBSOCKETS_H
#define NET_WEBSOCKETS_H

#include "net_defs.h"

extern net_module_t net_websockets_module;

#endif /* #ifndef NET_WEBSOCKETS_H */
