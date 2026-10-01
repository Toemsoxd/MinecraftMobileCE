#ifndef MC_CE_RENDERER_H
#define MC_CE_RENDERER_H

#include <windows.h>
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
    HWND m_hwnd;
    HDC m_dc;
    HBITMAP m_bitmap;
    HBITMAP m_oldBitmap;
    void* m_pixels;
    int m_width;
    int m_height;

    unsigned short* m_depth;

    bool CreateFramebuffer();
    void DestroyFramebuffer();
    void Clear(DWORD color);
    void DrawTerrain(const Terrain& terrain, const Camera& camera);

    void PutPixel(int x, int y, float depth, DWORD color);
    void DrawTriangle(float x0, float y0, float z0,
                      float x1, float y1, float z1,
                      float x2, float y2, float z2,
                      DWORD color);

    bool Project(float x, float y, float z,
                 const Camera& camera,
                 float* sx, float* sy, float* sz);
};

#endif
