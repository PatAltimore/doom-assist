//
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//     List of features which can be enabled/disabled to slim down the
//     program.
//

#ifndef DOOM_FEATURES_H
#define DOOM_FEATURES_H

// Enables wad merging (the '-merge' command line parameter)

#undef FEATURE_WAD_MERGE

// Enables dehacked support ('-deh')

#undef FEATURE_DEHACKED

// Enables multiplayer support (network games)
//
// doom-assist patch: enabled for __EMSCRIPTEN__ -- see net_websockets.c
// for the transport this build uses instead of a real UDP socket, and
// d_loop.c's own doom-assist patches for the other two things enabling
// this alone doesn't cover (net_sdl_module -> net_websockets_module, and
// un-guarding D_StartNetGame's real negotiation body).

#ifdef __EMSCRIPTEN__
#define FEATURE_MULTIPLAYER
#else
#undef FEATURE_MULTIPLAYER
#endif

// Enables sound output

//#undef FEATURE_SOUND

#endif /* #ifndef DOOM_FEATURES_H */


