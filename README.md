# MinecraftMobileCE

From-scratch Minecraft 0.6.x-style demake targeting Windows CE .NET 4.2 / eMbedded Visual C++ 4.0 SP2.

## Milestone 1

- Full 256x256 procedural terrain generated in memory.
- Deterministic seed.
- Direct3D 8 fixed-function 3D rendering.
- Hardware device with software vertex processing requested.
- Direct3D depth buffer / Z testing.
- Single batched terrain draw call per frame.
- Spectator camera.
- WASD movement and Space ascent.
- Arrow keys temporarily rotate the camera.
- Camera-local terrain submission to keep the first prototype practical on old ARM hardware.

## Target graphics API

This prototype uses Direct3D 8.

Windows CE .NET 4.2 officially documents Direct3DCreate8, IDirect3D8, IDirect3DDevice8, and D3d8.lib. The renderer creates a Direct3D 8 device, uses the fixed-function pipeline, enables the depth buffer, and submits the visible terrain as colored triangles.

The project no longer depends on DirectDraw.

## Build

Link against:

- d3d8.lib
- the normal Windows CE system libraries supplied by the eMbedded Visual C++ 4.0 / CE .NET 4.2 SDK.

Microsoft's CE 4.2 documentation lists D3d8.h and D3d8.lib as the header and link library for the Direct3D 8 interfaces.

## Controls

W/A/S/D: move
Space: move upward
Arrow keys: temporary camera look
Q/O/I/P/L: reserved for the Minecraft gameplay input layer.
