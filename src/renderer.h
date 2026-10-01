#ifndef MC_CE_RENDERER_H
#define MC_CE_RENDERER_H
#include <windows.h>
#include <d3d8.h>
#include "terrain.h"
struct Camera { float x,y,z,yaw,pitch; };
class Renderer {
public:
    Renderer(); ~Renderer();
    bool Initialize(HWND hwnd,int width,int height);
    void Shutdown();
    void Render(const Terrain& terrain,const Camera& camera);
private:
    IDirect3D8* m_d3d; IDirect3DDevice8* m_device; HWND m_hwnd; int m_width,m_height;
    void SetupMatrices(const Camera& camera);
    void DrawTerrain(const Terrain& terrain,const Camera& camera);
};
#endif
