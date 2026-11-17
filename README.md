# Solaris — Interactive C++17 Solar System

Solaris is a native GLFW + legacy OpenGL solar-system simulator. It retains illustrative distances and time scaling so the full system remains easy to explore. The scene uses deterministic procedural OpenGL shading and geometry; no external textures or runtime asset dependencies are required.

## Visual features

- White orbital paths and crisp white planet labels with dark translucent backing.
- Clickable colored lower-right controls for **PAUSE/PLAY**, **RESET**, **LABELS**, **ORBITS**, **SOUND**, **FOLLOW**, and **INFO**. Buttons brighten on hover and are also available through keyboard shortcuts.
- A physical, tilted, multi-band ring system on **Saturn only**. Other planets have no attached rings or circular selection outlines.
- Per-planet procedural surface variation: Mercury's cratered tones, Venus's cloudy bands, Earth's ocean/land hints, Mars's mottling, gas-giant banding, and ice-giant color variation. The Moon remains a cratered gray sphere.
- A denser, brighter starfield with restrained color and size variation; an asteroid belt of small, individually varied rocky points; and 32 continuously moving meteorites with bright heads and fading trails.
- Smoothly tessellated, lit planetary spheres, a bright Sun, subtle planet-specific atmospheric halos, selection/hover corner brackets, and a selected-planet information card.

## Windows and CLion

1. Open the `solar_system` folder (the folder containing `CMakeLists.txt`) in CLion.
2. Select a Windows **MinGW** toolchain (CLion's bundled MinGW is supported) or Visual Studio toolchain.
3. Allow CMake to configure and fetch GLFW 3.4; internet access is needed on first configure.
4. Select the **solaris** target, build, then run it.

If replacing an existing project, extract the archive or replace the complete project folder. If configuration/build errors persist after replacement, close the project and delete the old `cmake-build-debug` and/or `cmake-build-release` directory, reopen the project, reload CMake, and build the `solaris` target. On Windows, the project links `winmm` conditionally for `PlaySoundA`.

## Command-line Windows build

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\solaris.exe
```

## Controls

The colored on-screen actions are clickable as well as keyboard-accessible.

| Input | Action |
|---|---|
| Left-drag | Rotate the camera |
| Mouse wheel | Zoom in/out |
| Hover a planet | Show selection brackets and white label emphasis |
| Click a planet | Select it and play a selection sound (unless muted) |
| `1`–`8` | Select Mercury through Neptune |
| `Space` | Pause/resume simulation |
| `+` / `-` | Increase/decrease simulation speed |
| `F` | Toggle follow for the selected planet |
| `I` | Show/hide the information panel |
| `L` | Toggle planet labels |
| `T` | Toggle white orbit paths |
| `M` | Mute/unmute interface sounds |
| `R` | Reset camera and simulation time |
| `Esc` | Quit |

The project uses CMake 3.20+, C++17, GLFW 3.4 through FetchContent, and `OpenGL::GL`; Windows builds additionally link `winmm`. No framework replacement or external texture package is used.
