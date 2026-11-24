# SourceKrillion native app

A native C++ desktop version of SourceKrillion for **macOS and Windows**, made to look and play like the web version. It doesn't use a web view.

- **UI:** [RmlUi](https://github.com/mikke89/RmlUi) lays out `assets/ui/game.rml` and `game.src.rcss`. These use the same markup and classes as the web page, with values taken from the web stylesheet.
- **Ocean:** a line-by-line C++ port of `public/ocean.mjs` and `seascape.mjs`, drawn at half resolution (as the web canvas is) with [canvas_ity](https://github.com/a-e-k/canvas_ity) and a fast pixel blitter.
- **Sprites:** the same SVGs as the web, rendered with lunasvg.
- **Sound:** the same cue table as `public/sound.mjs`, synthesized sample by sample (sine and triangle oscillators, noise, swept biquad filters, linear attack and exponential decay).
- **Window, input and audio:** SDL3. **Networking:** IXWebSocket + mbedTLS (HTTP and party WebSockets).

## Online and offline

- **Online:** the app plays through your SourceKrillion server, so daily scores, friends, the archive, packs progress and Puffer Party all work. Each time it connects, it downloads a bundle with today's full answer lists, the Unlimited pool and the packs.
- **Offline:** it keeps playing from the last downloaded bundle: today's daily dive (if it was downloaded today), Unlimited and packs, scored on your device. Friends, the archive and Puffer Party need a connection.

## Pointing it at your server

The default is `http://localhost:3051`. Change it in **Settings → server address**, start it with `--server https://your.server`, or set `SOURCEKRILLION_SERVER`. The address is remembered.

## Build

Requirements: CMake 3.24+, git, and a C++20 compiler (Xcode command-line tools on macOS, Visual Studio 2022 with the C++ workload on Windows). All libraries are downloaded and linked statically on the first build, which takes a few minutes.

```sh
./build.sh         # macOS / Linux  -> build/SourceKrillion.app
build.bat          # Windows        -> build\Release\SourceKrillion.exe
```

Or download the prebuilt zips from the repository's Releases page. They're built by `.github/workflows/native.yml`. The macOS app isn't signed: the first time, right-click → Open, or run `xattr -cr SourceKrillion.app`.

Your data (player cookie, settings, offline bundle) lives in the OS app-data folder: `~/Library/Application Support/SourceKrillion` on macOS, `%APPDATA%\SourceKrillion` on Windows.

## Known differences from the web version

- The notched "pixel-frame" corners on the prompt card, HUD and labels are square in the app.
- Symbols that VT323 lacks (▼ ∞ ⟲ ☻) use DejaVu Sans. The web uses your system's fallback font.
- Text fields have no blinking-caret colour, and **F** toggles fullscreen as on the web.
