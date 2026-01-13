# Renderer

A small C++ OpenGL renderer with a blocky, chunked world (grass/dirt/stone).
Textures are loaded from `textures/blocks` and only visible block faces are generated.

## Controls

- Mouse: look around
- W/A/S/D: move
- Space: jump (tap)
- Double tap Space: toggle fly mode
- Space (fly mode): up
- Left Shift (fly mode): down
- Esc: quit

## Build

```bash
cmake -S . -B build
cmake --build build
./build/renderer
```
