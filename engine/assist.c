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
#include <limits.h>
#include <emscripten.h>

#include "doomdef.h"    // gamestate_t, GS_LEVEL, ML_MAPPED/ML_DONTDRAW
#include "doomstat.h"   // gameepisode, gamemap, paused, gamestate, players[]
#include "g_game.h"      // G_SaveGame, G_LoadGame
#include "p_saveg.h"     // P_SaveGameFile -- builds the on-disk save filename
#include "r_state.h"     // numlines, lines[], numsectors, sectors[]
#include "p_mobj.h"      // mobj_t, mobjinfo_t
#include "p_local.h"     // thinkercap, P_MobjThinker -- for walking live things (keys)

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
// this to swap Fire/Use for tap-to-select menu navigation (see
// assist_menu_* in m_menu.c) whenever a menu might be showing, since
// there'd otherwise be no way to navigate one by touch at all.
EMSCRIPTEN_KEEPALIVE int assist_get_menuactive(void)
{
    extern boolean menuactive; // m_menu.c
    return menuactive ? 1 : 0;
}

// The attract-mode demo (played automatically whenever the game has sat
// idle at the title screen) runs as a completely real GS_LEVEL -- demos
// are just a level plus a prerecorded input stream instead of a live
// player, so gamestate alone can't tell "actually playing" apart from
// "watching the demo loop" the way assist_get_menuactive's own comment
// already has to for menus. Without this, a touch player would have no
// way to ever interrupt the demo and reach the menu at all: the touch UI
// would treat the demo as real gameplay and offer Fire/Use instead of a
// way to open the menu.
EMSCRIPTEN_KEEPALIVE int assist_get_demoplayback(void)
{
    return demoplayback ? 1 : 0;
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
// The forward/back scale factor isn't arbitrary: it's picked so a *full*
// push moves at the same rate as holding down the run-speed keyboard
// equivalent (forwardmove[1]==50 -- see its definition in g_game.c), so
// "full stick" feels like "holding the fastest keyboard input", and
// anything less than full deflection feels proportionally slower, like
// a real analog stick.
//
// Strafe and turn are both scaled down further, to 60% of their
// keyboard-equivalents (sidemove[1]==40 -> 24, angleturn[1]==1280 ->
// 768): at the full rate, the short throw of a touch stick made a full
// push feel twitchy/oversensitive on those two axes -- small, easy-to-
// overshoot finger movements swung the view or slid the player sideways
// too far. 60% keeps "full stick" as the fastest speed available on
// each axis, just reached with a gentler slope. Forward/back doesn't
// get the same treatment: overshooting straight-line movement isn't the
// same kind of disorienting as overshooting a turn or a sideways slide.
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
        *side += assist_move_dx * 24 / 100;     // +-100 -> +-24 == 60% of sidemove[1]
    if (assist_turn_dx)
        // Matches the sign of the keyboard/mouse turn code just above
        // this function's call site: pushing right (positive dx) turns
        // right, which *decreases* angleturn.
        *angleturn -= (short)(assist_turn_dx * 768 / 100); // +-100 -> +-768 == 60% of angleturn[1]
}

// -----------------------------------------------------------------------
// Fog-of-war map
// -----------------------------------------------------------------------
// wolf3d-assist had to build its own explored-tile tracker from scratch,
// because Wolf3D has no automap at all. DOOM already has one (am_map.c),
// including its own fog-of-war -- every line has an ML_MAPPED flag the
// engine sets the instant it's been seen, which is exactly the "have I
// explored this?" bit a fog-of-war map needs -- so there's nothing to
// track here either. What DOOM's automap *doesn't* do is call out
// secrets, keys, teleporters, or the exit; those are the reason for a
// second, custom-drawn map in the sidebar (web/shell.html) instead of
// just pointing the Automap cheat button at the built-in one.
//
// All coordinates below are converted from DOOM's internal fixed-point
// (fixed_t, 16.16) down to plain map units via >>FRACBITS before being
// handed to JS -- there's no reason for the browser side to know or care
// about fixed-point.

#define ASSIST_MAXLINES 4096 // generous headroom over any shareware map's real line count
static int assist_line_buf[ASSIST_MAXLINES * 5]; // per line: x1,y1,x2,y2,kind

// kind values written into assist_line_buf; 0 means "don't draw this
// line" (unseen, or ML_DONTDRAW), matching AM_drawWalls' own skip rule.
#define ASSIST_LINE_NONE   0
#define ASSIST_LINE_WALL   1 // one-sided -- a solid, unpassable wall
#define ASSIST_LINE_INNER  2 // two-sided -- a step, door, or open boundary

// Recomputed on every call rather than cached: numlines for a shareware
// map tops out in the low hundreds, so re-walking it a couple of times a
// second (however often web/shell.html polls the Map tab) costs nothing
// worth caching for -- see this project's other perf notes (e.g.
// pollCurrentLevel in shell.html) for where that tradeoff has actually
// mattered, which is walking thousands of entries every single frame,
// not hundreds a couple of times a second.
EMSCRIPTEN_KEEPALIVE int assist_get_map_line_count(void)
{
    return numlines < ASSIST_MAXLINES ? numlines : ASSIST_MAXLINES;
}

EMSCRIPTEN_KEEPALIVE int *assist_get_map_lines(void)
{
    int i, n = assist_get_map_line_count();
    for (i = 0; i < n; i++)
    {
        line_t *ld = &lines[i];
        int *out = &assist_line_buf[i * 5];
        int kind = ASSIST_LINE_NONE;

        // Mirrors AM_drawWalls' own visibility rule exactly (am_map.c):
        // only a line the player has actually walked past (ML_MAPPED)
        // draws, and even then never one flagged never-draw.
        if ((ld->flags & ML_MAPPED) && !(ld->flags & ML_DONTDRAW))
            kind = ld->backsector ? ASSIST_LINE_INNER : ASSIST_LINE_WALL;

        out[0] = ld->v1->x >> FRACBITS;
        out[1] = ld->v1->y >> FRACBITS;
        out[2] = ld->v2->x >> FRACBITS;
        out[3] = ld->v2->y >> FRACBITS;
        out[4] = kind;
    }
    return assist_line_buf;
}

// Map bounds in the same map-unit space as assist_get_map_lines, so
// shell.html can scale/center the canvas to whatever this level's actual
// size is instead of a guessed or hardcoded one. Unlike the line buffer
// above, this covers the *whole* level regardless of fog-of-war -- it's
// just a coordinate range, not a spoiler, the same way a paper map's
// edges don't reveal what's drawn on it.
static int assist_bounds_buf[4]; // xmin, ymin, xmax, ymax

EMSCRIPTEN_KEEPALIVE int *assist_get_map_bounds(void)
{
    int i, n = numlines;
    int xmin = INT_MAX, ymin = INT_MAX, xmax = INT_MIN, ymax = INT_MIN;
    // Same "no level has loaded yet" window as assist_scan_pois's guard --
    // harmless here either way (numlines is 0, so the loop below just
    // never runs), but without this, an early caller would get
    // INT_MAX/INT_MIN back instead of a real (if empty) box, which breaks
    // shell.html's span/scale math outright rather than just drawing
    // nothing.
    if (n == 0)
    {
        assist_bounds_buf[0] = assist_bounds_buf[1] = assist_bounds_buf[2] = assist_bounds_buf[3] = 0;
        return assist_bounds_buf;
    }
    for (i = 0; i < n; i++)
    {
        line_t *ld = &lines[i];
        int xs[2] = { ld->v1->x >> FRACBITS, ld->v2->x >> FRACBITS };
        int ys[2] = { ld->v1->y >> FRACBITS, ld->v2->y >> FRACBITS };
        int j;
        for (j = 0; j < 2; j++)
        {
            if (xs[j] < xmin) xmin = xs[j];
            if (xs[j] > xmax) xmax = xs[j];
            if (ys[j] < ymin) ymin = ys[j];
            if (ys[j] > ymax) ymax = ys[j];
        }
    }
    assist_bounds_buf[0] = xmin;
    assist_bounds_buf[1] = ymin;
    assist_bounds_buf[2] = xmax;
    assist_bounds_buf[3] = ymax;
    return assist_bounds_buf;
}

// Points of interest: secrets, keys, teleporters, and the exit. Shown
// unconditionally, regardless of fog-of-war -- unlike the walls above,
// these are the whole point of this feature (per the user: "highlight
// secrets, keys, portals, exits... to help you finish a level"), a
// deliberate strategy-guide-style spoiler layered on top of an otherwise
// honest explored-so-far map, not an accident of how it's implemented.
//
// Secret sectors: sector->special is a plain vanilla-DOOM sector special
// number, and 9 means "secret, not yet entered" (P_PlayerInSpecialSector,
// p_spec.c, sets it back to 0 the instant the player steps into it) --
// so a live scan for special==9 already gives exactly the secrets not
// yet found, with no bookkeeping needed on this end either.
//
// Keys: found by walking the live thinker list (thinkercap, p_tick.c)
// for mobj_t entries -- identified the same way the engine itself tells
// a "thinker" apart from a "mobj thinker", by comparing its think
// function against P_MobjThinker -- and checking each one's doomednum
// (mobj->info->doomednum, i.e. the WAD thing-type number, confirmed
// against info.c: 5/13/6 are the blue/red/yellow keyCARDs, 39/38/40 the
// yellow/red/blue skull keys) against the six numbers that mean "a key
// card or skull is sitting here, uncollected." Once picked up, a key's
// mobj is removed from this list entirely, so -- like secrets above --
// an already-found key just naturally stops appearing, with no separate
// "found" tracking needed.
//
// Teleporters, the exit, and locked doors are all just specific
// line->special numbers, confirmed directly against the case labels that
// actually call EV_Teleport/G_ExitLevel/G_SecretExitLevel/EV_VerticalDoor/
// EV_DoLockedDoor (p_spec.c, p_switch.c, p_doors.c) rather than assumed
// from memory. Marked at each line's midpoint. A locked door's special
// stays put (so it keeps showing) unless it's the D1 "opens once,
// permanently" variant (32/33/34), which -- like a found secret --
// clears itself back to a plain special the instant it's used
// (EV_VerticalDoor, p_doors.c), so a door you've already opened for good
// naturally stops needing the reminder; the DR "opens every time, if you
// still have the key" variant (26/27/28) never clears and neither do the
// remote/switch-triggered lock checks (99/133-137), so those stay marked
// for the rest of the level.
#define ASSIST_POI_SECRET     1
#define ASSIST_POI_KEY_BLUE   2
#define ASSIST_POI_KEY_RED    3
#define ASSIST_POI_KEY_YELLOW 4
#define ASSIST_POI_TELEPORT   5
#define ASSIST_POI_EXIT       6
#define ASSIST_POI_DOOR_BLUE   7
#define ASSIST_POI_DOOR_RED    8
#define ASSIST_POI_DOOR_YELLOW 9
#define ASSIST_POI_SWITCH      10

#define ASSIST_MAXPOI 64
static int assist_poi_buf[ASSIST_MAXPOI * 3]; // per POI: x, y, type
static int assist_poi_count;

// A line "is a switch" the same way a player recognizes one: it's
// textured with one of the animated switch graphics (the ones that
// visibly flip to their "pressed" look), not by its special number --
// plenty of non-switch lines have specials too (walk-over teleporters,
// for one). switchlist (p_switch.c) is the engine's own already-loaded
// table of every valid switch texture *number* for this WAD's episode
// (built once at startup by P_InitSwitchList, from P_Init -- long before
// any level, let alone this scan, could ever run), so this just checks a
// line's sidedef textures against it instead of re-deriving the list
// from scratch. p_switch.c defines these as plain globals but never
// declares them extern in a header (p_spec.h only has the switchlist_t
// struct type), so we declare them ourselves here.
extern int switchlist[];
extern int numswitches;

static int assist_texture_is_switch(int texnum)
{
    int i;
    if (texnum <= 0)
        return 0;
    for (i = 0; i < numswitches * 2; i++)
        if (switchlist[i] == texnum)
            return 1;
    return 0;
}

static int assist_line_has_switch(line_t *ld)
{
    int s;
    for (s = 0; s < 2; s++)
    {
        int sidenum = ld->sidenum[s];
        if (sidenum < 0 || sidenum >= numsides)
            continue;
        if (assist_texture_is_switch(sides[sidenum].toptexture) ||
            assist_texture_is_switch(sides[sidenum].midtexture) ||
            assist_texture_is_switch(sides[sidenum].bottomtexture))
            return 1;
    }
    return 0;
}

static void assist_poi_add(int x, int y, int type)
{
    if (assist_poi_count >= ASSIST_MAXPOI)
        return;
    assist_poi_buf[assist_poi_count * 3 + 0] = x;
    assist_poi_buf[assist_poi_count * 3 + 1] = y;
    assist_poi_buf[assist_poi_count * 3 + 2] = type;
    assist_poi_count++;
}

static void assist_scan_pois(void)
{
    int i;
    thinker_t *th;

    assist_poi_count = 0;

    // thinkercap (p_tick.c) is a circular sentinel node that only becomes
    // self-referencing once P_InitThinkers() runs, which happens as part
    // of loading a level -- before that (e.g. shell.html's poll firing in
    // the brief window right after boot, before even the attract-mode
    // demo has loaded its own level), thinkercap.next is still its
    // zero-initialized default, NULL, and walking it as a list would
    // dereference that NULL immediately. numsectors is 0 for that exact
    // same "no level has ever loaded yet" window, and 0 the rest of the
    // time only during a state that could never call this in the first
    // place, so it doubles as a cheap, always-correct guard here.
    if (numsectors == 0)
        return;

    for (i = 0; i < numsectors; i++)
        if (sectors[i].special == 9) // secret, not yet entered
            assist_poi_add(sectors[i].soundorg.x >> FRACBITS,
                            sectors[i].soundorg.y >> FRACBITS,
                            ASSIST_POI_SECRET);

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        mobj_t *mo;
        int type;
        if (th->function.acp1 != (actionf_p1)P_MobjThinker)
            continue;
        mo = (mobj_t *)th;
        switch (mo->info->doomednum)
        {
            case 5:  type = ASSIST_POI_KEY_BLUE;   break;
            case 40: type = ASSIST_POI_KEY_BLUE;   break;
            case 13: type = ASSIST_POI_KEY_RED;    break;
            case 38: type = ASSIST_POI_KEY_RED;    break;
            case 6:  type = ASSIST_POI_KEY_YELLOW; break;
            case 39: type = ASSIST_POI_KEY_YELLOW; break;
            default: type = 0; break;
        }
        if (type)
            assist_poi_add(mo->x >> FRACBITS, mo->y >> FRACBITS, type);
    }

    for (i = 0; i < numlines; i++)
    {
        int special = lines[i].special;
        int type = 0;
        if (special == 39 || special == 97 || special == 125 || special == 126)
            type = ASSIST_POI_TELEPORT;
        else if (special == 11 || special == 51 || special == 52 || special == 124)
            type = ASSIST_POI_EXIT;
        else if (special == 26 || special == 32 || special == 99 || special == 133)
            type = ASSIST_POI_DOOR_BLUE;
        else if (special == 28 || special == 33 || special == 134 || special == 135)
            type = ASSIST_POI_DOOR_RED;
        else if (special == 27 || special == 34 || special == 136 || special == 137)
            type = ASSIST_POI_DOOR_YELLOW;
        else if (special != 0 && assist_line_has_switch(&lines[i]))
            // Fallback, not a first check: a switch-textured line with one
            // of the specific specials above (e.g. a keycard-locked door
            // that also happens to use a switch graphic) already has a
            // more useful marker; this only fires for the ones that
            // don't, which in practice means ordinary unlock-a-secret /
            // open-a-passage switches -- exactly the ones a player asking
            // "where's the switch for this secret" wants pointed out.
            type = ASSIST_POI_SWITCH;
        if (type)
            assist_poi_add((lines[i].v1->x + lines[i].v2->x) / 2 >> FRACBITS,
                            (lines[i].v1->y + lines[i].v2->y) / 2 >> FRACBITS,
                            type);
    }
}

EMSCRIPTEN_KEEPALIVE int assist_get_poi_count(void)
{
    assist_scan_pois();
    return assist_poi_count;
}

// Always call assist_get_poi_count() first -- it's what actually runs the
// scan; this just hands back the buffer that call filled in, the same
// two-step pattern assist_get_map_lines/assist_get_map_line_count uses.
EMSCRIPTEN_KEEPALIVE int *assist_get_poi_data(void)
{
    return assist_poi_buf;
}

// Player position (map units) and facing (degrees, 0-359, DOOM's
// counterclockwise-from-east convention) for the "you are here" marker.
// angle_t is a full 32-bit binary angle measure (BAM) -- turning it into
// degrees is just rescaling the whole unsigned range down to 0-359.
EMSCRIPTEN_KEEPALIVE int assist_get_player_x(void)
{
    return players[consoleplayer].mo ? players[consoleplayer].mo->x >> FRACBITS : 0;
}
EMSCRIPTEN_KEEPALIVE int assist_get_player_y(void)
{
    return players[consoleplayer].mo ? players[consoleplayer].mo->y >> FRACBITS : 0;
}
EMSCRIPTEN_KEEPALIVE int assist_get_player_angle(void)
{
    if (!players[consoleplayer].mo)
        return 0;
    return (int)(((double)players[consoleplayer].mo->angle / 4294967296.0) * 360.0);
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
