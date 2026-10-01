#include "renderer.h"
#include <math.h>

static const float PI = 3.14159265358979323846f;
static const float NEAR_Z = 0.15f;
static const float FAR_Z = 256.0f;
static const int TERRAIN_RADIUS = 14;
static const int MAX_TERRAIN_QUADS =
    (TERRAIN_RADIUS * 2 + 1) * (TERRAIN_RADIUS * 2 + 1) * 5;
static const int MAX_TERRAIN_VERTICES = MAX_TERRAIN_QUADS * 6;

struct Vertex {
    float x;
    float y;
    float z;
    DWORD color;
};

static const DWORD VERTEX_FVF = D3DFVF_XYZ | D3DFVF_DIFFUSE;
static Vertex g_vertices[MAX_TERRAIN_VERTICES];

static DWORD Color(float r, float g, float b)
{
    int ir = (int)(r * 255.0f);
    int ig = (int)(g * 255.0f);
    int ib = (int)(b * 255.0f);

    if (ir < 0) ir = 0;
    if (ir > 255) ir = 255;
    if (ig < 0) ig = 0;
    if (ig > 255) ig = 255;
    if (ib < 0) ib = 0;
    if (ib > 255) ib = 255;

    return D3DCOLOR_XRGB(ir, ig, ib);
}

static void Identity(D3DMATRIX* m)
{
    ZeroMemory(m, sizeof(*m));
    m->_11 = 1.0f;
    m->_22 = 1.0f;
    m->_33 = 1.0f;
    m->_44 = 1.0f;
}

static void MakeProjection(D3DMATRIX* m, float fovY,
                           float aspect, float zn, float zf)
{
    float yScale = 1.0f / (float)tan(fovY * 0.5f);
    float xScale = yScale / aspect;

    ZeroMemory(m, sizeof(*m));
    m->_11 = xScale;
    m->_22 = yScale;
    m->_33 = zf / (zf - zn);
    m->_34 = 1.0f;
    m->_43 = (-zn * zf) / (zf - zn);
}

static void MakeView(const Camera& c, D3DMATRIX* m)
{
    float cy = (float)cos(c.yaw);
    float sy = (float)sin(c.yaw);
    float cp = (float)cos(c.pitch);
    float sp = (float)sin(c.pitch);

    float rx = cy;
    float rz = -sy;

    float ux = sy * sp;
    float uy = cp;
    float uz = cy * sp;

    float fx = sy * cp;
    float fy = sp;
    float fz = cy * cp;

    ZeroMemory(m, sizeof(*m));

    m->_11 = rx;
    m->_12 = ux;
    m->_13 = fx;
    m->_14 = 0.0f;

    m->_21 = 0.0f;
    m->_22 = uy;
    m->_23 = fy;
    m->_24 = 0.0f;

    m->_31 = rz;
    m->_32 = uz;
    m->_33 = fz;
    m->_34 = 0.0f;

    m->_41 = -(rx * c.x + rz * c.z);
    m->_42 = -(ux * c.x + uy * c.y + uz * c.z);
    m->_43 = -(fx * c.x + fy * c.y + fz * c.z);
    m->_44 = 1.0f;
}

static void AddQuad(Vertex* out, int* count,
                    float x0, float y0, float z0,
                    float x1, float y1, float z1,
                    float x2, float y2, float z2,
                    float x3, float y3, float z3,
                    DWORD color)
{
    int i = *count;

    out[i + 0].x = x0; out[i + 0].y = y0; out[i + 0].z = z0; out[i + 0].color = color;
    out[i + 1].x = x1; out[i + 1].y = y1; out[i + 1].z = z1; out[i + 1].color = color;
    out[i + 2].x = x2; out[i + 2].y = y2; out[i + 2].z = z2; out[i + 2].color = color;
    out[i + 3].x = x0; out[i + 3].y = y0; out[i + 3].z = z0; out[i + 3].color = color;
    out[i + 4].x = x2; out[i + 4].y = y2; out[i + 4].z = z2; out[i + 4].color = color;
    out[i + 5].x = x3; out[i + 5].y = y3; out[i + 5].z = z3; out[i + 5].color = color;

    *count = i + 6;
}

Renderer::Renderer()
    : m_d3d(0), m_device(0), m_hwnd(0), m_width(240), m_height(320)
{
}

Renderer::~Renderer()
{
    Shutdown();
}

bool Renderer::Initialize(HWND hwnd, int width, int height)
{
    m_hwnd = hwnd;
    m_width = width;
    m_height = height;

    m_d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!m_d3d)
        return false;

    if (!CreateDevice(hwnd, width, height)) {
        Shutdown();
        return false;
    }

    return true;
}

bool Renderer::CreateDevice(HWND hwnd, int width, int height)
{
    D3DPRESENT_PARAMETERS pp;
    HRESULT hr;

    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = width;
    pp.BackBufferHeight = height;
    pp.BackBufferFormat = D3DFMT_R5G6B5;
    pp.BackBufferCount = 1;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = FALSE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D16;
    pp.Flags = 0;
    pp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

    hr = m_d3d->CreateDevice(D3DADAPTER_DEFAULT,
                             D3DDEVTYPE_HAL,
                             hwnd,
                             D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                             &pp,
                             &m_device);

    if (FAILED(hr) || !m_device)
        return false;

    m_device->SetRenderState(D3DRS_LIGHTING, FALSE);
    m_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    m_device->SetRenderState(D3DRS_ZENABLE, TRUE);
    m_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    m_device->SetFVF(VERTEX_FVF);

    return true;
}

void Renderer::Shutdown()
{
    if (m_device) {
        m_device->Release();
        m_device = 0;
    }

    if (m_d3d) {
        m_d3d->Release();
        m_d3d = 0;
    }
}

void Renderer::SetCamera(const Camera& c)
{
    D3DMATRIX view;
    D3DMATRIX projection;
    float aspect = (float)m_width / (float)m_height;

    MakeView(c, &view);
    MakeProjection(&projection,
                   70.0f * PI / 180.0f,
                   aspect,
                   NEAR_Z,
                   FAR_Z);

    m_device->SetTransform(D3DTS_VIEW, &view);
    m_device->SetTransform(D3DTS_PROJECTION, &projection);
}

void Renderer::DrawTerrain(const Terrain& t, const Camera& c)
{
    int cx = (int)c.x;
    int cz = (int)c.z;
    int radius = TERRAIN_RADIUS;
    int vertexCount = 0;
    int z;

    DWORD grass = Color(0.31f, 0.67f, 0.27f);
    DWORD side1 = Color(0.37f, 0.27f, 0.17f);
    DWORD side2 = Color(0.31f, 0.23f, 0.16f);
    DWORD side3 = Color(0.27f, 0.20f, 0.14f);

    for (z = cz - radius; z <= cz + radius; ++z) {
        int x;

        if (z < 0 || z >= WORLD_SIZE)
            continue;

        for (x = cx - radius; x <= cx + radius; ++x) {
            int h, l, r, f, b;

            if (x < 0 || x >= WORLD_SIZE)
                continue;

            h = t.GetHeight(x, z);
            l = (x > 0) ? t.GetHeight(x - 1, z) : h;
            r = (x < WORLD_SIZE - 1) ? t.GetHeight(x + 1, z) : h;
            f = (z > 0) ? t.GetHeight(x, z - 1) : h;
            b = (z < WORLD_SIZE - 1) ? t.GetHeight(x, z + 1) : h;

            if (vertexCount + 6 <= MAX_TERRAIN_VERTICES)
                AddQuad(g_vertices, &vertexCount,
                        (float)x, (float)h, (float)z,
                        (float)x + 1.0f, (float)h, (float)z,
                        (float)x + 1.0f, (float)h, (float)z + 1.0f,
                        (float)x, (float)h, (float)z + 1.0f,
                        grass);

            if (l < h && vertexCount + 6 <= MAX_TERRAIN_VERTICES)
                AddQuad(g_vertices, &vertexCount,
                         (float)x, (float)l, (float)z,
                         (float)x, (float)h, (float)z,
                         (float)x, (float)h, (float)z + 1.0f,
                         (float)x, (float)l, (float)z + 1.0f,
                         side1);

            if (r < h && vertexCount + 6 <= MAX_TERRAIN_VERTICES)
                AddQuad(g_vertices, &vertexCount,
                         (float)x + 1.0f, (float)l, (float)z + 1.0f,
                         (float)x + 1.0f, (float)h, (float)z + 1.0f,
                         (float)x + 1.0f, (float)h, (float)z,
                         (float)x + 1.0f, (float)l, (float)z,
                         side2);

            if (f < h && vertexCount + 6 <= MAX_TERRAIN_VERTICES)
                AddQuad(g_vertices, &vertexCount,
                         (float)x, (float)f, (float)z,
                         (float)x + 1.0f, (float)f, (float)z,
                         (float)x + 1.0f, (float)h, (float)z,
                         (float)x, (float)h, (float)z,
                         side3);

            if (b < h && vertexCount + 6 <= MAX_TERRAIN_VERTICES)
                AddQuad(g_vertices, &vertexCount,
                         (float)x + 1.0f, (float)b, (float)z + 1.0f,
                         (float)x, (float)b, (float)z + 1.0f,
                         (float)x, (float)h, (float)z + 1.0f,
                         (float)x + 1.0f, (float)h, (float)z + 1.0f,
                         side1);
        }
    }

    if (vertexCount > 0) {
        m_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
                                  vertexCount / 3,
                                  g_vertices,
                                  sizeof(Vertex));
    }
}

void Renderer::Render(const Terrain& t, const Camera& c)
{
    D3DMATRIX world;
    HRESULT hr;

    if (!m_device)
        return;

    Identity(&world);
    m_device->SetTransform(D3DTS_WORLD, &world);
    SetCamera(c);

    hr = m_device->Clear(0, 0,
                         D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                         D3DCOLOR_XRGB(115, 185, 235),
                         1.0f, 0);
    if (FAILED(hr))
        return;

    hr = m_device->BeginScene();
    if (FAILED(hr))
        return;

    DrawTerrain(t, c);

    m_device->EndScene();
    m_device->Present(0, 0, 0, 0);
}
