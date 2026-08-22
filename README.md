# doom-assist

A browser-playable build of the original 1993 *DOOM* (shareware episode, "Knee-Deep in the Dead"), with an "assist mode" sidebar (real cheat codes, the game's own automap, per-map hints) that links out to [Code Museum](https://blue-rock-0e6a0831e.7.azurestaticapps.net/#/doom)'s annotated source pages for the underlying code.

## What's here

- `engine/` — [ozkl/doomgeneric](https://github.com/ozkl/doomgeneric) (a portability fork of id Software's [linuxdoom-1.10](https://github.com/id-Software/DOOM/tree/master/linuxdoom-1.10) source that isolates platform-specific video/sound/input behind a handful of functions and already ships a working Emscripten target), vendored directly rather than as a git submodule -- it needs two small local patches (below), which a plain submodule pin can't carry across a fresh clone.
- `engine/assist.c` — the assist-mode C exports (level/kill/secret readers, autosave, the touch joystick) that `web/shell.html` calls into. Two small patches on top of doomgeneric itself: one line in `g_game.c` to feed the touch joystick into the same `mousex`/`mousey` path real mouse motion would use, and one line in `doomgeneric_emscripten.c` to point saved games at the browser's persistent storage. Both are marked `--- doom-assist patch ---` inline.
- `web/shell.html` — the custom Emscripten shell: the assist-mode UI (cheats, automap toggle, hints, recent actions) wrapped around the game canvas.
- `data/shareware/DOOM1.WAD` — the original 1995 shareware episode data (v1.9, `sha1:5b2e249b9c5133ec987b3ea77596381dc0d6bc1d`). id Software has permitted free redistribution of this file since the game shipped as shareware; the full registered game's data is not included (and isn't bundled/downloadable from here).
- `staticwebapp.config.json`, `.github/workflows/azure-static-web-apps.yml` — Azure Static Web Apps (free tier) deployment.

## Running locally

```bash
# 1. Get the Emscripten SDK (one-time; ~1GB, not checked into the repo)
git clone https://github.com/emscripten-core/emsdk.git tools/emsdk
cd tools/emsdk && python emsdk.py install 6.0.6 && python emsdk.py activate 6.0.6 && cd ../..

# 2. Activate it in your shell
source tools/emsdk-env.sh

# 3. Build the engine
cd engine && bash build-emscripten.sh && cd ..

# 4. Serve it (can't open the file directly -- browsers block wasm/fetch
#    requests from file:// URLs)
cd engine && python -m http.server 8090

# 5. Open http://localhost:8090/index.html
```

If `tools/emsdk` already exists (e.g. from a previous setup), skip straight to step 2.

## Deployment

Pushing to `main` triggers `.github/workflows/azure-static-web-apps.yml`, which builds the engine fresh in CI and deploys the result to Azure Static Web Apps using the `AZURE_STATIC_WEB_APPS_API_TOKEN` repo secret.

## License / credits

- DOOM source: id Software, released under GPL-2.0 in 1999 (see `engine/license-gpl.txt`). This project is personal and non-commercial.
- Engine portability layer: [doomgeneric](https://github.com/ozkl/doomgeneric), GPL-2.0.
- Shareware game data: id Software, freely redistributable since 1993.
- Code Museum annotations linked from assist mode: [PatAltimore/code-museum](https://github.com/PatAltimore/code-museum).
