#ifndef MC_CE_RENDERER_H
#define MC_CE_RENDERER_H

#include <windows.h>
#include <ddraw.h>
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
    LPDIRECTDRAW4 m_dd;
    LPDIRECTDRAWSURFACE5 m_primary;
    HWND m_hwnd;
    int m_width;
    int m_height;
    float m_zbuffer[240 * 320];

    void DrawTerrain(const Terrain& terrain, const Camera& camera,
                     unsigned short* pixels, int pitchBytes);
    void DrawQuad(const Camera& camera, unsigned short* pixels, int pitchBytes,
                  float x0, float y0, float z0,
                  float x1, float y1, float z1,
                  float x2, float y2, float z2,
                  float x3, float y3, float z3,
                  unsigned short color);
    void DrawTriangle(const Camera& camera, unsigned short* pixels, int pitchBytes,
                      float x0, float y0, float z0,
                      float x1, float y1, float z1,
                      float x2, float y2, float z2,
                      unsigned short color);
};

#endif
