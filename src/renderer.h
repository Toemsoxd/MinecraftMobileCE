#ifndef MC_CE_RENDERER_H
#define MC_CE_RENDERER_H

#include <windows.h>
#include "terrain.h"

typedef unsigned short Pixel;

struct Camera {
    float x;
    float y;
    float z;
    float yaw;
    float pitch;
};

struct Vertex {
    float x;
    float y;
    float z;
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

    float m_camCosYaw;
    float m_camSinYaw;
    float m_camCosPitch;
    float m_camSinPitch;

    float m_projScaleX;
    float m_projScaleY;

    float m_cachedYaw;
    float m_cachedPitch;
    bool m_cameraCacheValid;

    bool CreateFramebuffer();
    void DestroyFramebuffer();

    void Clear(Pixel color);

    Vertex WorldToView(float x, float y, float z,
                       const Camera& camera) const;

    bool Project(const Vertex& vertex,
                 float* screenX,
                 float* screenY) const;

    void DrawViewTriangle(Vertex a, Vertex b, Vertex c,
                          Pixel color);

    void DrawTriangle(float x0, float y0, float z0,
                      float x1, float y1, float z1,
                      float x2, float y2, float z2,
                      Pixel color);

    void DrawQuad(const Vertex& a, const Vertex& b,
                  const Vertex& c, const Vertex& d,
                  Pixel color);

    void DrawTerrain(const Terrain& terrain,
                     const Camera& camera);
};

#endif
