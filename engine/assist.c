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
// Twin-stick touch controls
// -----------------------------------------------------------------------
// Two independent sticks (web/shell.html): a left one for movement
// (forward/back and strafe left/right) and a right one for turning.
// Firing isn't handled here at all -- the right stick's touch handler
// just dispatches the real Fire key (Control) on pointerdown/pointerup,
// the same synthetic-keyboard trick every cheat button already uses, so
// a tap fires once and holding it fires continuously, exactly like a
// real held key would for an automatic weapon.
//
// An earlier, single-stick version of this feature reused mousex/mousey
// (meant for real relative mouse motion, unused elsewhere in this
// Emscripten build) as a shortcut -- but mousex's meaning in
// G_BuildTiccmd flips between "turn" and "strafe" depending on whether
// the strafe key happens to be held, which only works for a single
// combined stick. A twin-stick scheme needs strafe and turn to be two
// independently-addressable axes at all times, so this version instead
// adds straight into forward/side/angleturn -- three pointers
// G_BuildTiccmd (g_game.c) passes in from its own local variables, right
// after every other input source (keyboard, real mouse, joystick) has
// already had its say and right before they're all clamped together.
//
// The scale factors below aren't arbitrary: they're picked so a *full*
// push moves/strafes/turns at the same rate as holding down the
// run-speed keyboard equivalent (forwardmove[1]==50, sidemove[1]==40,
// angleturn[1]==1280 -- see their definitions in g_game.c), so "full
// stick" feels like "holding the fastest keyboard input", and anything
// less than full deflection feels proportionally slower, like a real
// analog stick.
static int assist_move_dx = 0, assist_move_dy = 0; // -100..100, left stick
static int assist_turn_dx = 0;                     // -100..100, right stick

EMSCRIPTEN_KEEPALIVE void assist_set_move(int dx, int dy)
{
    assist_move_dx = dx;
    assist_move_dy = dy;
}

EMSCRIPTEN_KEEPALIVE void assist_set_turn(int dx)
{
    assist_turn_dx = dx;
}

// Not EMSCRIPTEN_KEEPALIVE: this isn't called from JS, only from the
// engine's own control-building code (see the forward declaration and
// call site added to G_BuildTiccmd in g_game.c).
void assist_apply_touch_controls(int *forward, int *side, short *angleturn)
{
    if (assist_move_dy)
        // dy is screen-space (web/shell.html's pointer delta): pushing
        // the stick UP is a *negative* dy (up the screen), but forward
        // motion needs a *positive* contribution here -- confirmed
        // backwards without this negation.
        *forward -= assist_move_dy * 50 / 100;  // +-100 -> +-50 == forwardmove[1]
    if (assist_move_dx)
        *side += assist_move_dx * 40 / 100;     // +-100 -> +-40 == sidemove[1]
    if (assist_turn_dx)
        // Matches the sign of the keyboard/mouse turn code just above
        // this function's call site: pushing right (positive dx) turns
        // right, which *decreases* angleturn.
        *angleturn -= (short)(assist_turn_dx * 1280 / 100); // +-100 -> +-1280 == angleturn[1]
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
