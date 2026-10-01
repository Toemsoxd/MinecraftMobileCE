# MinecraftMobileCE

From-scratch Minecraft 0.6.x-style demake targeting Windows CE .NET 4.2 / eMbedded Visual C++ 4.0 SP2.

## Milestone 1

- Full 256x256 procedural terrain generated in memory.
- Deterministic seed.
- Software 3D rasterizer written for the project.
- DirectDraw output to the Windows CE primary surface.
- 16-bit RGB565 framebuffer.
- Z-buffered terrain triangles.
- Spectator camera.
- WASD movement and Space ascent.
- Arrow keys temporarily rotate the camera.
- Camera-local terrain submission to keep the first prototype practical on old ARM hardware.

## Target graphics API

This prototype uses DirectDraw, not Direct3D.

Windows CE .NET 4.2 documents DirectDraw through ddraw.h / ddraw.lib. The renderer creates an IDirectDraw4, obtains the primary surface as IDirectDrawSurface5, locks the surface, and runs the 3D rasterizer entirely in software.

This avoids requiring a Direct3D 3D driver for the Minecraft renderer itself.

## Build

Link against:

- ddraw.lib
- the normal Windows CE system libraries supplied by the eMbedded Visual C++ 4.0 / CE .NET 4.2 SDK.

## Controls

W/A/S/D: move
Space: move upward
Arrow keys: temporary camera look
Q/O/I/P/L: reserved for the Minecraft gameplay input layer.
