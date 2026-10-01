#ifndef MC_CE_RENDERER_H
#define MC_CE_RENDERER_H

#include <windows.h>
#include <d3d8.h>
#include "terrain.h"

struct Camera {
    float x;
    float y;
    float z;
    float yaw;
    float pitch;
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool Initialize(HWND hwnd, int width, int height);
    void Shutdown();
    void Render(const Terrain& terrain, const Camera& camera);

private:
    LPDIRECT3D8 m_d3d;
    LPDIRECT3DDEVICE8 m_device;
    HWND m_hwnd;
    int m_width;
    int m_height;

    void DrawTerrain(const Terrain& terrain, const Camera& camera);
    bool CreateDevice(HWND hwnd, int width, int height);
    void SetCamera(const Camera& camera);
};

#endif
