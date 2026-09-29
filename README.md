# Crash Forge: Racing

**Crash Forge: Racing** is a native PC re-engineering of *Crash Team Racing* (PS1, 1999), built on top of the [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK) decompilation project.

The goal is not only to run CTR natively on modern PCs, but to turn it into a more open, expandable and mod-friendly version of the game: higher refresh rates, native rendering improvements, dynamic racer mods, custom music, PC-focused menus and fewer limitations inherited from the original PlayStation hardware.

> **Work in progress:** this project is actively being developed. Some features, especially experimental 3D wheel rendering, may still change.

The executable and some internal symbols currently keep the historical name **CTR Native** (`ctr_native.exe`). The project itself is now **Crash Forge: Racing**.

---

## Highlights

### Native PC port

- Native Windows and Linux builds.
- SDL3-based platform layer.
- OpenGL native renderer.
- Static Windows build with vendored SDL3.
- Runs from the original CTR game data instead of emulating a PlayStation.

### High refresh rate support

CTR's original game logic was designed around 30 FPS. Crash Forge: Racing keeps the original gameplay timing while allowing presentation at modern monitor refresh rates.

Current work includes:

- High-refresh rendering.
- Automatic monitor refresh-rate targeting.
- Improved frame pacing after stalls.
- Interpolated presentation where appropriate.
- Water animation cached to CTR's logical tick instead of being recalculated unnecessarily on every rendered frame.

### Native graphics options

The PC options menus include:

- Resolution.
- Windowed / fullscreen display mode.
- 4:3 and 16:9 presentation.
- High Refresh mode.
- Subpixel rendering.
- Character Detail: **LOW / SMART / HIGH / ULTRA**.

Higher detail modes extend racer and particle visibility beyond the original PS1-oriented limits while lower modes remain available for slower hardware.

The native renderer also includes work for improved subpixel stability and perspective-correct texture handling.

### Mouse-friendly PC menus

Menus can be operated directly with the system mouse instead of treating the mouse as a virtual gamepad.

Current mouse support includes:

- Vanilla menus.
- Native/custom PC option menus.
- Character, cup and track selection.
- Mouse-driven audio sliders.
- Battle settings.
- The full on-screen name-entry keyboard.
- The **YES / NO** exit confirmation.
- Context-sensitive **SKIP** and **△ BACK** buttons.
- **SKIP** support for the Naughty Dog crate intro and the CTR title animation.

Keyboard and controller input remain supported.

### Dynamic racer mods

Custom racers can be installed without rebuilding the game.

Each racer lives in:

```text
assets/mods/racers/<slug>/
```

A racer package can provide OBJ geometry, MTL materials, textures, display name, retail fallback character, engine class, model scale/offsets, icon settings and wheel configuration.

Example `character.ini`:

```ini
[character]
name = Example Racer
enabled = true
fallback_retail = crash
engine = balanced
has_wheels = false

[model]
scale = 1.0
offset_x = 0.0
offset_y = 0.0
offset_z = 0.0

[assets]
asset_name = example_racer
icon = retail:crash
```

The game discovers racer folders automatically on startup.

### Native OBJ rendering

Crash Forge: Racing includes a native OBJ/MTL loader for custom racer models.

Supported features include:

- Polygonal OBJ faces.
- UV coordinates.
- MTL materials.
- PNG/BMP textures.
- Optional vertex colors.
- Model caching.
- Per-racer scale and position offsets.
- Named wheel objects: `wheel_fl`, `wheel_fr`, `wheel_rl`, `wheel_rr`.

Custom OBJ racers use a native single-header model path instead of inheriting the original PS1 distance-LOD behavior.

### Experimental 3D retail wheels

The project includes an experimental global 3D-wheel override for retail racers.

It can:

- Replace retail wheel cards with native OBJ wheel geometry.
- Preserve CTR's wheel positions and sizes.
- Rotate wheels with kart movement.
- Steer the front wheels.
- Orient geometry with the kart instead of camera-facing billboards.
- Fall back to the original 2D wheels if 3D geometry cannot be emitted.
- Apply across gameplay, ghosts and character previews.

This system is still under active development.

### Custom music

Levels can override their normal music with OGG files.

Place files in:

```text
assets/MUSIC_CUSTOM/
```

Naming:

```text
level_XX.ogg
level_XX_final.ogg
```

`XX` is the two-digit internal level ID.

The optional `_final.ogg` file is used for the final lap. If it is missing, the normal custom track continues.

The native custom-music system supports mono/stereo OGG Vorbis, pre-caching, final-lap switching, pitch-preserving speed handling and hub/music timeline handling.

### Original disc + extracted asset overrides

A fully extracted asset folder is **not** required for normal play.

The game can read directly from a retail NTSC-U CTR disc image:

```text
assets/ctr-u.bin
```

Extracted files are still supported as development/modding overrides and take priority when present.

### Native performance diagnostics

Internal builds include profiling support for investigating frametime problems instead of relying only on average FPS.

Performance captures can track game logic, render submission, level geometry, water, particles, VRAM updates, framebuffer work, swap/presentation and audio timing.

Each performance session is stored in its own timestamped/versioned folder with:

```text
frame_times.csv
summary.txt
runtime.log
```

---

## Project philosophy

- **Remove PS1-era limits when they are no longer useful.**
- **Preserve CTR gameplay behavior unless a change is intentional.**
- **Keep native platform code separate from game logic.**
- **Make modding data-driven whenever possible.**
- **Prefer PC-native capabilities over artificial console restrictions.**
- **Keep the original retail game data external to the source repository.**

---

## Directory layout

```text
Crash-Forge-Racing/
  main.c
  CMakeLists.txt
  build.bat
  build.sh

  game/
  include/
  platform/
  externals/SDL/
  metadata/
  docs/
  tools/
  tests/
```

- `game/` contains the decompiled game-side source used by the native build.
- `platform/` contains PC/native implementations and compatibility layers.
- `include/` contains game structures, declarations and native interfaces.
- `externals/SDL/` contains the vendored SDL3 source.

---

## Game data

The repository does **not** include copyrighted CTR game assets.

For normal play, provide your own NTSC-U retail CTR disc image as:

```text
assets/ctr-u.bin
```

The expected format is a raw single-track PlayStation BIN using MODE2/2352 sectors. A cooked 2048-byte ISO does not preserve the XA/STR sector data required for the original audio/video content.

Example runtime layout:

```text
Crash Forge - Racing/
  ctr_native.exe
  assets/
    ctr-u.bin
```

---

## Building

### Windows

1. Install [MSYS2](https://www.msys2.org/).
2. In an MSYS2 terminal:

```bash
pacman -Syu
pacman -S --needed git mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-make
```

3. Add `C:\msys64\mingw32\bin` to your system `PATH`.
4. Run:

```bat
build.bat
```

Output:

```text
build/ctr_native.exe
```

SDL3 is built from the vendored source automatically.

### Linux

Install the required 32-bit and desktop development packages:

```bash
sudo apt install gcc-multilib
sudo apt install libx11-dev libxext-dev libgl1-mesa-dev libasound2-dev libudev-dev libdbus-1-dev
```

Then:

```bash
chmod +x build.sh
./build.sh
```

Output:

```text
build/ctr_native
```

### Clean build

Windows:

```bat
rmdir /s /q build
build.bat
```

Linux:

```bash
rm -rf build/
./build.sh
```

---

## Running a development build

When running directly from `build/`, keep the assets folder next to the source tree:

```text
Crash-Forge-Racing/
  build/
    ctr_native.exe
  assets/
    ctr-u.bin
```

---

## Extracted asset overrides

Extracted files are optional. When present under `assets/`, they override matching data from `ctr-u.bin` and are useful for development, debugging and modding.

Common override paths include:

```text
assets/
  BIGFILE.BIG
  SOUNDS/
    KART.HWL
  TEST.STR
  XA/
  MUSIC_CUSTOM/
  mods/
    racers/
```

---

## Development notes

### Architecture

```text
main.c
  |
  +-- platform/native_*
  |
  +-- game/game_unity.h
        |
        +-- game/
              |
              +-- include/
```

`CTR_NATIVE` marks host-specific/native code.

The current build remains 32-bit while remaining PS1 address-shaped structures and host-pointer assumptions are audited and migrated. GPU primitive links are bridged through native tokens; see [docs/MEMORY_MODEL.md](docs/MEMORY_MODEL.md).

Internal builds also contain logs, replay tools and performance diagnostics for development work.

---

## Current status

Crash Forge: Racing is not intended to be a byte-for-byte preservation build. It is an evolving native PC version of CTR focused on keeping the original driving/game logic recognizable while improving presentation, opening fixed systems to mods and gradually replacing inherited PS1 limitations with native PC systems where it is safe to do so.

Expect active development and experimental features.

---

## Credits

- [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK) — decompilation project this work is built on.
- [PsyCross](https://github.com/OpenDriver2/PsyCross) — source of parts of the PS1 compatibility approach used by the native platform layer.
- [SDL3](https://github.com/libsdl-org/SDL) — cross-platform windowing, input and platform support.
- Xiph.Org / libogg / libvorbis — OGG Vorbis support used by the native custom-music system.

*Crash Team Racing* and related characters, names and assets belong to their respective rights holders. **Crash Forge: Racing** is an unofficial fan project and is not affiliated with or endorsed by Sony Interactive Entertainment, Naughty Dog or Activision.
