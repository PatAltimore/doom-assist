#!/bin/bash
# Standalone Emscripten build for doomgeneric (no `make` dependency).
# doomgeneric's source is vendored flat into this directory (see README.md
# for why -- it needs two small local patches, which a plain git submodule
# can't carry). Adapted from doomgeneric's own Makefile.emscripten object
# list, plus our assist.c. Run from the engine/ directory after sourcing
# tools/emsdk-env.sh.
set -e

# Exact source list from doomgeneric's own Makefile.emscripten (SRC_DOOM),
# minus nothing -- kept in sync with upstream rather than hand-pruned, so a
# doomgeneric update is a simple diff against that Makefile.
SRCS=(
  dummy.c am_map.c doomdef.c doomstat.c dstrings.c d_event.c d_items.c
  d_iwad.c d_loop.c d_main.c d_mode.c d_net.c f_finale.c f_wipe.c g_game.c
  hu_lib.c hu_stuff.c info.c i_cdmus.c i_endoom.c i_joystick.c i_scale.c
  i_sound.c i_system.c i_timer.c memio.c m_argv.c m_bbox.c m_cheat.c
  m_config.c m_controls.c m_fixed.c m_menu.c m_misc.c m_random.c p_ceilng.c
  p_doors.c p_enemy.c p_floor.c p_inter.c p_lights.c p_map.c p_maputl.c
  p_mobj.c p_plats.c p_pspr.c p_saveg.c p_setup.c p_sight.c p_spec.c
  p_switch.c p_telept.c p_tick.c p_user.c r_bsp.c r_data.c r_draw.c
  r_main.c r_plane.c r_segs.c r_sky.c r_things.c sha1.c sounds.c
  statdump.c st_lib.c st_stuff.c s_sound.c tables.c v_video.c wi_stuff.c
  w_checksum.c w_file.c w_main.c w_wad.c z_zone.c w_file_stdc.c i_input.c
  i_video.c doomgeneric.c doomgeneric_emscripten.c mus2mid.c i_sdlmusic.c
  i_sdlsound.c
)

CFLAGS="-DFEATURE_SOUND -sUSE_SDL=2 -sUSE_SDL_MIXER=2 -sSDL2_MIXER_FORMATS=['mid'] -O2"
LDFLAGS="-sASYNCIFY -sINITIAL_MEMORY=64MB -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web --preload-file doom1.wad --shell-file ../web/shell.html -sEXPORTED_FUNCTIONS=_main,_assist_get_episode,_assist_get_map,_assist_get_gamestate,_assist_get_kills,_assist_get_items,_assist_get_secrets,_assist_get_weapon,_assist_is_paused,_assist_toggle_pause,_assist_get_menuactive,_assist_get_demoplayback,_assist_has_autosave,_assist_autosave,_assist_resume,_assist_set_move,_assist_set_turn,_assist_get_map_line_count,_assist_get_map_lines,_assist_get_map_bounds,_assist_get_poi_count,_assist_get_poi_data,_assist_get_player_x,_assist_get_player_y,_assist_get_player_angle,_assist_menu_item_count,_assist_menu_base_y,_assist_menu_set_item -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8,HEAP32,FS -lidbfs.js -O2"

mkdir -p build-em
# doomgeneric's IWAD search (d_iwad.c) looks for lowercase "doom1.wad" in
# the working directory -- the preloaded virtual filesystem's root, since
# --preload-file below has no @-destination override.
cp ../data/shareware/DOOM1.WAD doom1.wad

OBJS=()
for src in "${SRCS[@]}"; do
  obj="build-em/${src%.*}.o"
  OBJS+=("$obj")
  echo "===> CC $src"
  emcc $CFLAGS -c "$src" -o "$obj"
done

echo "===> CC assist.c"
emcc $CFLAGS -c assist.c -o build-em/assist.o
OBJS+=(build-em/assist.o)

echo "===> LD index.html"
emcc $CFLAGS "${OBJS[@]}" $LDFLAGS -o index.html

echo "Build complete: engine/index.html, index.js, index.wasm, index.data"
