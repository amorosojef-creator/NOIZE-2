# NOIZE

A free, offline digital audio workstation (C++17 / JUCE). No account, no subscription, no cloud.
Futuristic dark UI (black/charcoal, chrome greys, neon `#CCFF00`).

**Version 0.1.0 — early release.** Read [docs/STATUS.md](docs/STATUS.md) for exactly what works, what was tested, and what is not done yet.

## Get it running

**Windows 10/11 (64-bit)** — a Windows `.exe` must be built on Windows (this was developed in a Linux container, where JUCE's Windows build can't run):
- *Easiest, no Windows PC needed:* put this folder on GitHub, open **Actions → "Build NOIZE for Windows" → Run workflow**, then download `NOIZE-windows` (contains `NOIZE-portable.zip` and `NOIZE-Setup.exe`).
- *On a Windows PC:* install Visual Studio 2022 Build Tools (C++ workload), CMake and Git once, then **double-click `build-windows.bat`**.

**macOS** — double-click `build-mac.command` (untested, see STATUS.md).

**Linux** — `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build` (tested).

Then open `examples/First_Light.noize` (File > Open) and press **Space**.

## Layout
- `Source/Core` — audio engine, instruments, effects, project model, WAV export (no GUI dependencies)
- `Source/UI` — workspaces and widgets
- `Source/Tests` — headless engine tests (`noize_tests`)
- `docs/` — user guide, status, licenses

Licensed under AGPLv3 because it uses JUCE under its AGPL terms. See [docs/LICENSES.md](docs/LICENSES.md).
