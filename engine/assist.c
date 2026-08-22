//
// assist.c -- doom-assist's browser sidebar support code.
//
// Everything in this file is new: it's not part of doomgeneric or the
// original id Software source. It exists purely to expose read-only
// snapshots of the engine's existing state to web/shell.html (the
// Emscripten shell that draws the assist-mode sidebar), and to accept a
// couple of inputs back from it (the touch joystick, autosave requests).
//
// Design note for readers new to this codebase: almost nothing here
// *computes* anything new. DOOM already tracks kills/secrets/items per
// player, already has a real automap and a real cheat-code system, and
// already has a working save/load path -- so instead of re-implementing
// any of that, every assist_get_* function below is just a thin,
// EMSCRIPTEN_KEEPALIVE-exported reader over a global the engine already
// maintains for its own purposes (see doomstat.h). That's also why this
// file is small: there was very little that actually needed inventing.
//
// Compare with wolf3d-assist's equivalent code in wl_game.cpp -- that
// project's engine (Wolfenstein 3D) had no automap and a hidden/locked
// cheat system, so its assist layer had to do much more work (a whole
// persistent "explored tiles" buffer, an unlock patch for cheats). DOOM's
// engine already does the interesting parts, so this file mostly just
// plumbs data through.

#include <stdio.h>
#include <string.h>
#include <emscripten.h>

#include "doomdef.h"    // gamestate_t, GS_LEVEL
#include "doomstat.h"   // gameepisode, gamemap, paused, gamestate, players[]
#include "g_game.h"      // G_SaveGame, G_LoadGame
#include "p_saveg.h"     // P_SaveGameFile -- builds the on-disk save filename

// -----------------------------------------------------------------------
// Level / progress readers
// -----------------------------------------------------------------------
// gameepisode/gamemap are set whenever a level starts and simply hold
// their last value afterwards (menus, intermissions, etc. don't reset
// them) -- so shell.html can poll these at any time and get a sensible
// "what level was last played" answer, the same way wolf3d-assist polls
// gamestate.mapon.
EMSCRIPTEN_KEEPALIVE int assist_get_episode(void) { return gameepisode; }
EMSCRIPTEN_KEEPALIVE int assist_get_map(void) { return gamemap; }

// GS_LEVEL means "actually playing a map right now" (as opposed to a menu,
// intermission screen, or the demo loop) -- shell.html uses this to avoid
// logging a "Reached Episode 1 Map 1" action from stale values while
// you're still sitting at the title screen.
EMSCRIPTEN_KEEPALIVE int assist_get_gamestate(void) { return (int)gamestate; }

// Feeds the "Recent Actions" log in shell.html: plain reads of player_t
// fields the game itself already maintains for the HUD/intermission
// screen (st_stuff.c, wi_stuff.c). JS polls and diffs these the same way
// wolf3d-assist's assist_get_kills/secrets are diffed on the JS side --
// no new bookkeeping needed here, just exposing what's already tracked.
EMSCRIPTEN_KEEPALIVE int assist_get_kills(void) { return players[consoleplayer].killcount; }
EMSCRIPTEN_KEEPALIVE int assist_get_items(void) { return players[consoleplayer].itemcount; }
EMSCRIPTEN_KEEPALIVE int assist_get_secrets(void) { return players[consoleplayer].secretcount; }
EMSCRIPTEN_KEEPALIVE int assist_get_weapon(void) { return (int)players[consoleplayer].readyweapon; }

// The engine's own pause flag (see G_Ticker/BTS_PAUSE in g_game.c).
// assist_is_paused() just lets the UI reflect whatever state the engine
// is actually in; assist_toggle_pause() drives the real pause the same
// way a keypress would.
EMSCRIPTEN_KEEPALIVE int assist_is_paused(void) { return paused ? 1 : 0; }

// Lets shell.html tell "actually playing" apart from "a menu (title,
// pause, skill/episode select, save/load, ...) is drawn over whatever's
// behind it" -- the two aren't the same thing (menuactive doesn't stop
// gameplay simulating behind it, see m_menu.c). The touch controls use
// this to swap Fire/Use for a small Up/Down/Select/Back cluster whenever
// a menu might be showing, since there'd otherwise be no way to navigate
// one by touch at all.
EMSCRIPTEN_KEEPALIVE int assist_get_menuactive(void)
{
    extern boolean menuactive; // m_menu.c
    return menuactive ? 1 : 0;
}

// wolf3d-assist's equivalent feature sends the engine's real Pause key
// and lets the existing keyboard handling do the rest. DOOM's Emscripten
// keyboard translation (doomgeneric_emscripten.c's convertToDoomKey)
// doesn't have a case for SDL's Pause key -- it falls through to the
// default branch, which mishandles it -- so rather than patch that
// translation table for one key, this goes straight to the same
// mechanism a real Pause keypress would ultimately trigger anyway:
// G_Responder (g_game.c) sets sendpause = true on the Pause keydown
// event, and G_Ticker toggles `paused` from that flag on the next tic.
// Setting the flag directly here skips the keyboard round-trip entirely.
EMSCRIPTEN_KEEPALIVE void assist_toggle_pause(void)
{
    extern boolean sendpause; // g_game.c
    sendpause = true;
}

// -----------------------------------------------------------------------
// Autosave / resume
// -----------------------------------------------------------------------
// Reserve save slot 8 (the last of vanilla DOOM's 8 save slots, 0-7) for
// the browser's autosave, so it never collides with a slot the player
// might use from the real in-game Save menu.
#define ASSIST_SAVE_SLOT 7
#define ASSIST_SAVE_DESC "Browser Autosave"

// G_SaveGame/G_LoadGame (g_game.c) don't write/read immediately -- they
// just record what to do and set a flag that G_Ticker acts on during the
// *next* game tic, exactly like the real Save/Load Game menu triggers
// them (see m_menu.c). Reusing them means the on-disk format always
// matches what the game's own Load Game screen expects, instead of this
// file needing to know DOOM's save format itself.
EMSCRIPTEN_KEEPALIVE int assist_has_autosave(void)
{
    FILE *f = fopen(P_SaveGameFile(ASSIST_SAVE_SLOT), "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

// Only meaningful mid-level (GS_LEVEL) -- calling G_SaveGame from the
// title screen or an intermission would queue a save of a game that
// isn't actually in progress.
EMSCRIPTEN_KEEPALIVE int assist_autosave(void)
{
    if (gamestate != GS_LEVEL)
        return 0;
    G_SaveGame(ASSIST_SAVE_SLOT, ASSIST_SAVE_DESC);
    return 1;
}

// The "Resume" button (shell.html) calls this directly rather than
// synthesizing a Load Game menu keypress sequence the way wolf3d-assist's
// Resume button has to. DOOM's G_LoadGame is already just a plain
// function taking a filename -- there's no menu state machine to drive
// through, so there's nothing to gain from pretending to click through
// menus a player never sees.
EMSCRIPTEN_KEEPALIVE int assist_resume(void)
{
    if (!assist_has_autosave())
        return 0;
    G_LoadGame(P_SaveGameFile(ASSIST_SAVE_SLOT));
    return 1;
}

// -----------------------------------------------------------------------
// Touch joystick
// -----------------------------------------------------------------------
// G_BuildTiccmd (g_game.c) reads two globals, mousex/mousey, and turns
// them into proportional turning/forward movement each tic: a small value
// turns/moves a little, a large one turns/moves a lot -- unlike the
// keyboard's on/off digital movement, where "held" always means full
// speed. Those globals are meant for real relative mouse motion, but
// nothing in this Emscripten build currently feeds them (the browser
// shell only forwards keyboard events, not mouse movement -- see
// doomgeneric_emscripten.c) -- so they're a free, already-analog input
// path this file can reuse for the touch joystick instead of teaching
// G_BuildTiccmd a second, parallel movement scheme.
//
// assist_apply_joystick() (called once per tic from a one-line patch in
// G_BuildTiccmd, right before it reads mousex/mousey) converts the
// joystick's -100..100 deflection into that same range mousex/mousey
// normally carry. The scale factors below aren't arbitrary: they're
// picked so a *full* push turns/moves at the same rate as holding down
// the run-turn/run-forward keys (angleturn[1]==1280, MAXPLMOVE==
// forwardmove[1]==50 -- see their definitions in g_game.c), so "full
// stick" feels like "holding the fastest keyboard input", and anything
// less than full deflection feels proportionally slower, like a real
// analog stick.
static int assist_joy_dx = 0, assist_joy_dy = 0; // -100..100, from web/shell.html

EMSCRIPTEN_KEEPALIVE void assist_set_joystick(int dx, int dy)
{
    assist_joy_dx = dx;
    assist_joy_dy = dy;
}

// Not EMSCRIPTEN_KEEPALIVE: this isn't called from JS, only from the
// engine's own control-polling code (see the forward declaration and call
// site added to G_BuildTiccmd in g_game.c).
void assist_apply_joystick(void)
{
    extern int mousex, mousey; // g_game.c (file-scope globals, not static)
    if (assist_joy_dx)
        mousex += assist_joy_dx * 8 / 5;  // +-100 -> +-160; G_BuildTiccmd applies *0x8, giving +-1280 == angleturn[1]
    if (assist_joy_dy)
        // dy is screen-space (web/shell.html's pointer delta): pushing the
        // stick UP is a *negative* dy (up the screen), but forward motion
        // needs a *positive* contribution to G_BuildTiccmd's `forward`.
        // Negate here so "up" reliably means "forward", not the other way
        // around -- confirmed backwards without this negation.
        mousey -= assist_joy_dy / 2;      // +-100 -> +-50 == MAXPLMOVE, G_BuildTiccmd adds this to forward directly
}

// -----------------------------------------------------------------------
// Startup
// -----------------------------------------------------------------------
// D_DoomMain (via doomgeneric_Create) picks a save directory automatically
// (m_config.c's M_GetSaveGameDir) based on the IWAD name -- fine for a
// real filesystem, but this build's saves need to land specifically in
// "/save/", the one path web/shell.html mounts as a persistent
// IndexedDB-backed filesystem (see the Module.preRun block in shell.html;
// everything else in the Emscripten virtual filesystem is memory-only and
// disappears on refresh). Overriding the global here, once, right after
// doomgeneric_Create() finishes its own startup, is simpler than teaching
// the engine's save-path logic about IndexedDB.
EMSCRIPTEN_KEEPALIVE void assist_init(void)
{
    extern char *savegamedir; // d_main.c
    savegamedir = "/save/";
}
