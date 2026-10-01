#include "renderer.h"
#include <math.h>

static const float PI = 3.14159265358979323846f;
static const float NEAR_Z = 0.15f;
static const float FAR_Z = 256.0f;

struct ProjectedVertex {
    float x;
    float y;
    float z;
    bool valid;
};

static unsigned short RGB565(int r, int g, int b)
{
    return (unsigned short)(((r >> 3) << 11) |
                            ((g >> 2) << 5) |
                            (b >> 3));
}

static ProjectedVertex Project(const Camera& c, int width, int height,
                               float wx, float wy, float wz)
{
    float dx = wx - c.x;
    float dy = wy - c.y;
    float dz = wz - c.z;
    float cy = (float)cos(c.yaw);
    float sy = (float)sin(c.yaw);
    float vx = cy * dx - sy * dz;
    float vz = sy * dx + cy * dz;
    float cp = (float)cos(c.pitch);
    float sp = (float)sin(c.pitch);
    float vy = cp * dy - sp * vz;
    float vz2 = sp * dy + cp * vz;
    ProjectedVertex p;
    p.valid = (vz2 > NEAR_Z && vz2 < FAR_Z);
    p.z = vz2;
    if (!p.valid) {
        p.x = p.y = 0.0f;
        return p;
    }
    {
        float focal = ((float)height * 0.5f) /
                      (float)tan(35.0f * PI / 180.0f);
        p.x = (float)width * 0.5f + (vx * focal / vz2);
        p.y = (float)height * 0.5f - (vy * focal / vz2);
    }
    return p;
}

static float Edge(float ax, float ay, float bx, float by,
                  float px, float py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

Renderer::Renderer()
    : m_dd(0), m_primary(0), m_hwnd(0), m_width(240), m_height(320)
{
    int i;
    for (i = 0; i < 240 * 320; ++i)
        m_zbuffer[i] = FAR_Z;
}

Renderer::~Renderer()
{
    Shutdown();
}

bool Renderer::Initialize(HWND hwnd, int width, int height)
{
    LPDIRECTDRAW ddBase = 0;
    LPDIRECTDRAWSURFACE4 primary4 = 0;
    DDSURFACEDESC2 desc;
    HRESULT hr;

    m_hwnd = hwnd;
    m_width = width;
    m_height = height;

    hr = DirectDrawCreate(0, &ddBase, 0);
    if (FAILED(hr))
        return false;

    hr = ddBase->QueryInterface(IID_IDirectDraw4, (void**)&m_dd);
    ddBase->Release();

    if (FAILED(hr) || !m_dd) {
        m_dd = 0;
        return false;
    }

    hr = m_dd->SetCooperativeLevel(hwnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
    if (FAILED(hr)) {
        Shutdown();
        return false;
    }

    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    hr = m_dd->CreateSurface(&desc, &primary4, 0);
    if (FAILED(hr) || !primary4) {
        Shutdown();
        return false;
    }

    hr = primary4->QueryInterface(IID_IDirectDrawSurface5,
                                  (void**)&m_primary);
    primary4->Release();

    if (FAILED(hr) || !m_primary) {
        m_primary = 0;
        Shutdown();
        return false;
    }

    return true;
}

void Renderer::Shutdown()
{
    if (m_primary) {
        m_primary->Release();
        m_primary = 0;
    }
    if (m_dd) {
        m_dd->RestoreDisplayMode();
        m_dd->SetCooperativeLevel(m_hwnd, DDSCL_NORMAL);
        m_dd->Release();
        m_dd = 0;
    }
}

void Renderer::DrawTriangle(const Camera& camera,
                            unsigned short* pixels, int pitchBytes,
                            float x0, float y0, float z0,
                            float x1, float y1, float z1,
                            float x2, float y2, float z2,
                            unsigned short color)
{
    ProjectedVertex a = Project(camera, m_width, m_height, x0, y0, z0);
    ProjectedVertex b = Project(camera, m_width, m_height, x1, y1, z1);
    ProjectedVertex c = Project(camera, m_width, m_height, x2, y2, z2);
    float area;
    int minX, maxX, minY, maxY;
    int y;

    if (!a.valid || !b.valid || !c.valid)
        return;

    area = Edge(a.x, a.y, b.x, b.y, c.x, c.y);
    if (area == 0.0f)
        return;

    minX = (int)floor(a.x);
    maxX = (int)ceil(a.x);
    minY = (int)floor(a.y);
    maxY = (int)ceil(a.y);

    if ((int)floor(b.x) < minX) minX = (int)floor(b.x);
    if ((int)ceil(b.x) > maxX) maxX = (int)ceil(b.x);
    if ((int)floor(c.x) < minX) minX = (int)floor(c.x);
    if ((int)ceil(c.x) > maxX) maxX = (int)ceil(c.x);
    if ((int)floor(b.y) < minY) minY = (int)floor(b.y);
    if ((int)ceil(b.y) > maxY) maxY = (int)ceil(b.y);
    if ((int)floor(c.y) < minY) minY = (int)floor(c.y);
    if ((int)ceil(c.y) > maxY) maxY = (int)ceil(c.y);

    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= m_width) maxX = m_width - 1;
    if (maxY >= m_height) maxY = m_height - 1;
    if (minX > maxX || minY > maxY)
        return;

    for (y = minY; y <= maxY; ++y) {
        unsigned short* row =
            (unsigned short*)((unsigned char*)pixels + y * pitchBytes);
        int x;
        for (x = minX; x <= maxX; ++x) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0 = Edge(b.x, b.y, c.x, c.y, px, py);
            float w1 = Edge(c.x, c.y, a.x, a.y, px, py);
            float w2 = Edge(a.x, a.y, b.x, b.y, px, py);

            if ((area > 0.0f && w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) ||
                (area < 0.0f && w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f)) {
                float invArea = 1.0f / area;
                float depth = (w0 * invArea) * a.z +
                              (w1 * invArea) * b.z +
                              (w2 * invArea) * c.z;
                int zi;
                if (depth > NEAR_Z && depth < FAR_Z) {
                    zi = y * m_width + x;
                    if (depth < m_zbuffer[zi]) {
                        m_zbuffer[zi] = depth;
                        row[x] = color;
                    }
                }
            }
        }
    }
}

void Renderer::DrawQuad(const Camera& camera,
                        unsigned short* pixels, int pitchBytes,
                        float x0, float y0, float z0,
                        float x1, float y1, float z1,
                        float x2, float y2, float z2,
                        float x3, float y3, float z3,
                        unsigned short color)
{
    DrawTriangle(camera, pixels, pitchBytes,
                 x0, y0, z0, x1, y1, z1, x2, y2, z2, color);
    DrawTriangle(camera, pixels, pitchBytes,
                 x0, y0, z0, x2, y2, z2, x3, y3, z3, color);
}

void Renderer::DrawTerrain(const Terrain& t, const Camera& c,
                           unsigned short* pixels, int pitchBytes)
{
    int cx = (int)c.x;
    int cz = (int)c.z;
    int radius = 14;
    unsigned short grass = RGB565(80, 170, 70);
    unsigned short side1 = RGB565(95, 70, 45);
    unsigned short side2 = RGB565(78, 58, 40);
    unsigned short side3 = RGB565(68, 50, 36);
    int z;

    for (z = cz - radius; z <= cz + radius; ++z) {
        if (z < 0 || z >= WORLD_SIZE)
            continue;

        {
            int x;
            for (x = cx - radius; x <= cx + radius; ++x) {
                int h, l, r, f, b;
                if (x < 0 || x >= WORLD_SIZE)
                    continue;

                h = t.GetHeight(x, z);
                l = t.GetHeight(x - 1, z);
                r = t.GetHeight(x + 1, z);
                f = t.GetHeight(x, z - 1);
                b = t.GetHeight(x, z + 1);

                DrawQuad(c, pixels, pitchBytes,
                         (float)x, (float)h, (float)z,
                         (float)x + 1.0f, (float)h, (float)z,
                         (float)x + 1.0f, (float)h, (float)z + 1.0f,
                         (float)x, (float)h, (float)z + 1.0f, grass);

                if (l < h)
                    DrawQuad(c, pixels, pitchBytes,
                             (float)x, (float)l, (float)z,
                             (float)x, (float)h, (float)z,
                             (float)x, (float)h, (float)z + 1.0f,
                             (float)x, (float)l, (float)z + 1.0f, side1);

                if (r < h)
                    DrawQuad(c, pixels, pitchBytes,
                             (float)x + 1.0f, (float)l, (float)z + 1.0f,
                             (float)x + 1.0f, (float)h, (float)z + 1.0f,
                             (float)x + 1.0f, (float)h, (float)z,
                             (float)x + 1.0f, (float)l, (float)z, side2);

                if (f < h)
                    DrawQuad(c, pixels, pitchBytes,
                             (float)x, (float)f, (float)z,
                             (float)x + 1.0f, (float)f, (float)z,
                             (float)x + 1.0f, (float)h, (float)z,
                             (float)x, (float)h, (float)z, side3);

                if (b < h)
                    DrawQuad(c, pixels, pitchBytes,
                             (float)x + 1.0f, (float)b, (float)z + 1.0f,
                             (float)x, (float)b, (float)z + 1.0f,
                             (float)x, (float)h, (float)z + 1.0f,
                             (float)x + 1.0f, (float)h, (float)z + 1.0f, side1);
            }
        }
    }
}

void Renderer::Render(const Terrain& t, const Camera& c)
{
    DDSURFACEDESC2 desc;
    HRESULT hr;
    unsigned short* pixels;
    int i;

    if (!m_primary)
        return;

    for (i = 0; i < m_width * m_height; ++i)
        m_zbuffer[i] = FAR_Z;

    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);

    hr = m_primary->Lock(0, &desc,
                         DDLOCK_WAIT | DDLOCK_WRITEONLY, 0);
    if (FAILED(hr))
        return;

    pixels = (unsigned short*)desc.lpSurface;

    for (i = 0; i < m_height; ++i) {
        unsigned short* row =
            (unsigned short*)((unsigned char*)pixels + i * desc.lPitch);
        int x;
        for (x = 0; x < m_width; ++x)
            row[x] = RGB565(115, 185, 235);
    }

    DrawTerrain(t, c, pixels, desc.lPitch);
    m_primary->Unlock(0);
}
