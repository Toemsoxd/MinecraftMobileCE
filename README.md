# MinecraftMobileCE

From-scratch Minecraft 0.6.x-style demake targeting Windows CE .NET 4.2 / eMbedded Visual C++ 4.0 SP2.

## Milestone 1
- Full 256x256 procedural terrain generated in memory.
- Deterministic seed.
- Fixed-function Direct3D 8 rendering.
- Spectator camera.
- WASD movement and Space ascent.
- Arrow keys temporarily rotate the camera.
- Camera-local terrain submission to keep the first prototype practical on old ARM hardware.

## Target API
Windows CE .NET 4.2 uses Direct3D 8 here: `d3d8.h` and `d3d8.lib`. Direct3D Mobile is a CE 5.0 API and is not used.

## Controls
W/A/S/D: move
Space: move upward
Arrow keys: temporary camera look
Q/O/I/P/L: reserved for the Minecraft gameplay input layer.
