#include "renderer.h"
#include <math.h>

static const float PI = 3.14159265358979323846f;
static const float FOV = 70.0f * PI / 180.0f;
static const float NEAR_Z = 0.10f;
static const float FAR_Z = 64.0f;
static const int TERRAIN_RADIUS = 14;

static DWORD Color(unsigned char r, unsigned char g, unsigned char b)
{
    return ((DWORD)r) | ((DWORD)g << 8) | ((DWORD)b << 16);
}

static float Min3(float a, float b, float c)
{
    float v = a;
    if (b < v) v = b;
    if (c < v) v = c;
    return v;
}

static float Max3(float a, float b, float c)
{
    float v = a;
    if (b > v) v = b;
    if (c > v) v = c;
    return v;
}

Renderer::Renderer()
    : m_hwnd(0), m_dc(0), m_bitmap(0), m_oldBitmap(0),
      m_pixels(0), m_width(240), m_height(320), m_depth(0)
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

    return CreateFramebuffer();
}

bool Renderer::CreateFramebuffer()
{
    BITMAPINFO bi;
    HDC screen;
    m_dc = 0;
    m_bitmap = 0;
    m_oldBitmap = 0;
    m_pixels = 0;
    m_depth = 0;

    screen = GetDC(m_hwnd);
    if (!screen)
        return false;

    m_dc = CreateCompatibleDC(screen);
    ReleaseDC(m_hwnd, screen);

    if (!m_dc)
        return false;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = m_width;
    bi.bmiHeader.biHeight = -m_height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    m_bitmap = CreateDIBSection(m_dc, &bi, DIB_RGB_COLORS,
                                &m_pixels, 0, 0);
    if (!m_bitmap || !m_pixels) {
        DestroyFramebuffer();
        return false;
    }

    m_oldBitmap = (HBITMAP)SelectObject(m_dc, m_bitmap);

    m_depth = new unsigned short[m_width * m_height];
    if (!m_depth) {
        DestroyFramebuffer();
        return false;
    }

    Clear(Color(115, 185, 235));
    return true;
}

void Renderer::DestroyFramebuffer()
{
    if (m_depth) {
        delete[] m_depth;
        m_depth = 0;
    }

    if (m_dc) {
        if (m_oldBitmap)
            SelectObject(m_dc, m_oldBitmap);

        if (m_bitmap)
            DeleteObject(m_bitmap);

        DeleteDC(m_dc);
    }

    m_dc = 0;
    m_bitmap = 0;
    m_oldBitmap = 0;
    m_pixels = 0;
}

void Renderer::Shutdown()
{
    DestroyFramebuffer();
}

void Renderer::Clear(DWORD color)
{
    DWORD* pixels;
    int count;
    int i;

    if (!m_pixels || !m_depth)
        return;

    pixels = (DWORD*)m_pixels;
    count = m_width * m_height;

    for (i = 0; i < count; ++i) {
        pixels[i] = color;
        m_depth[i] = 65535;
    }
}

void Renderer::PutPixel(int x, int y, float depth, DWORD color)
{
    DWORD* pixels;
    int index;
    unsigned short z;

    if (x < 0 || x >= m_width || y < 0 || y >= m_height)
        return;

    if (depth < NEAR_Z || depth > FAR_Z)
        return;

    z = (unsigned short)((depth / FAR_Z) * 65534.0f);
    index = y * m_width + x;

    if (z >= m_depth[index])
        return;

    m_depth[index] = z;
    pixels = (DWORD*)m_pixels;
    pixels[index] = color;
}

void Renderer::DrawTriangle(float x0, float y0, float z0,
                            float x1, float y1, float z1,
                            float x2, float y2, float z2,
                            DWORD color)
{
    float minX, maxX, minY, maxY;
    float area;
    float invArea;
    int ix0, ix1, iy0, iy1;
    int x, y;

    minX = Min3(x0, x1, x2);
    maxX = Max3(x0, x1, x2);
    minY = Min3(y0, y1, y2);
    maxY = Max3(y0, y1, y2);

    ix0 = (int)floor(minX);
    ix1 = (int)ceil(maxX);
    iy0 = (int)floor(minY);
    iy1 = (int)ceil(maxY);

    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 >= m_width) ix1 = m_width - 1;
    if (iy1 >= m_height) iy1 = m_height - 1;

    area = (x1 - x0) * (y2 - y0) -
           (y1 - y0) * (x2 - x0);

    if (area > -0.001f && area < 0.001f)
        return;

    invArea = 1.0f / area;

    for (y = iy0; y <= iy1; ++y) {
        for (x = ix0; x <= ix1; ++x) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0, w1, w2;
            float depth;

            w0 = ((x1 - x0) * (py - y0) -
                  (y1 - y0) * (px - x0)) * invArea;
            w1 = ((x2 - x1) * (py - y1) -
                  (y2 - y1) * (px - x1)) * invArea;
            w2 = 1.0f - w0 - w1;

            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                depth = w0 * z2 + w1 * z0 + w2 * z1;
                PutPixel(x, y, depth, color);
            }
        }
    }
}

bool Renderer::Project(float x, float y, float z,
                       const Camera& c,
                       float* sx, float* sy, float* sz)
{
    float dx = x - c.x;
    float dy = y - c.y;
    float dz = z - c.z;
    float cy = (float)cos(c.yaw);
    float syaw = (float)sin(c.yaw);
    float cp = (float)cos(c.pitch);
    float sp = (float)sin(c.pitch);
    float vx, vy, vz;
    float ux, uy, uz;
    float aspect;
    float scale;

    /* Camera forward points along +Z when yaw/pitch are zero. */
    vx = dx * cy - dz * syaw;
    vz = dx * syaw + dz * cy;

    vy = dy;

    ux = vx;
    uy = vy * cp + vz * sp;
    uz = -vy * sp + vz * cp;

    if (uz <= NEAR_Z)
        return false;

    if (uz > FAR_Z)
        return false;

    aspect = (float)m_width / (float)m_height;
    scale = (float)tan(FOV * 0.5f);

    *sx = (float)m_width * 0.5f +
          (ux / (uz * scale * aspect)) * (float)m_width * 0.5f;

    *sy = (float)m_height * 0.5f -
          (uy / (uz * scale)) * (float)m_height * 0.5f;

    *sz = uz;
    return true;
}

void Renderer::AddQuad(float x0, float y0, float z0,
                       float x1, float y1, float z1,
                       float x2, float y2, float z2,
                       float x3, float y3, float z3,
                       DWORD color, const Camera& c)
{
    float sx0, sy0, sz0;
    float sx1, sy1, sz1;
    float sx2, sy2, sz2;
    float sx3, sy3, sz3;
    bool p0, p1, p2, p3;

    p0 = Project(x0, y0, z0, c, &sx0, &sy0, &sz0);
    p1 = Project(x1, y1, z1, c, &sx1, &sy1, &sz1);
    p2 = Project(x2, y2, z2, c, &sx2, &sy2, &sz2);
    p3 = Project(x3, y3, z3, c, &sx3, &sy3, &sz3);

    if (p0 && p1 && p2)
        DrawTriangle(sx0, sy0, sz0, sx1, sy1, sz1,
                     sx2, sy2, sz2, color);

    if (p0 && p2 && p3)
        DrawTriangle(sx0, sy0, sz0, sx2, sy2, sz2,
                     sx3, sy3, sz3, color);
}

void Renderer::DrawTerrain(const Terrain& t, const Camera& c)
{
    int cx = (int)c.x;
    int cz = (int)c.z;
    int radius = TERRAIN_RADIUS;
    int z;

    DWORD grass = Color(79, 171, 69);
    DWORD side1 = Color(94, 69, 43);
    DWORD side2 = Color(79, 59, 41);
    DWORD side3 = Color(69, 51, 36);

    for (z = cz - radius; z <= cz + radius; ++z) {
        int x;

        if (z < 0 || z >= WORLD_SIZE)
            continue;

        for (x = cx - radius; x <= cx + radius; ++x) {
            int h;
            int l;
            int rr;
            int f;
            int b;

            if (x < 0 || x >= WORLD_SIZE)
                continue;

            h = t.GetHeight(x, z);
            l = (x > 0) ? t.GetHeight(x - 1, z) : h;
            rr = (x < WORLD_SIZE - 1) ? t.GetHeight(x + 1, z) : h;
            f = (z > 0) ? t.GetHeight(x, z - 1) : h;
            b = (z < WORLD_SIZE - 1) ? t.GetHeight(x, z + 1) : h;

            AddQuad(
                    (float)x, (float)h, (float)z,
                    (float)x + 1.0f, (float)h, (float)z,
                    (float)x + 1.0f, (float)h, (float)z + 1.0f,
                    (float)x, (float)h, (float)z + 1.0f,
                    grass, c);

            if (l < h)
                AddQuad(
                        (float)x, (float)l, (float)z,
                        (float)x, (float)h, (float)z,
                        (float)x, (float)h, (float)z + 1.0f,
                        (float)x, (float)l, (float)z + 1.0f,
                        side1, c);

            if (rr < h)
                AddQuad(
                        (float)x + 1.0f, (float)l, (float)z + 1.0f,
                        (float)x + 1.0f, (float)h, (float)z + 1.0f,
                        (float)x + 1.0f, (float)h, (float)z,
                        (float)x + 1.0f, (float)l, (float)z,
                        side2, c);

            if (f < h)
                AddQuad(
                        (float)x, (float)f, (float)z,
                        (float)x + 1.0f, (float)f, (float)z,
                        (float)x + 1.0f, (float)h, (float)z,
                        (float)x, (float)h, (float)z,
                        side3, c);

            if (b < h)
                AddQuad(
                        (float)x + 1.0f, (float)b, (float)z + 1.0f,
                        (float)x, (float)b, (float)z + 1.0f,
                        (float)x, (float)h, (float)z + 1.0f,
                        (float)x + 1.0f, (float)h, (float)z + 1.0f,
                        side1, c);
        }
    }
}

void Renderer::Render(const Terrain& t, const Camera& c)
{
    if (!m_dc || !m_pixels)
        return;

    Clear(Color(115, 185, 235));
    DrawTerrain(t, c);

    {
        HDC screen = GetDC(m_hwnd);

        if (screen) {
            BitBlt(screen, 0, 0, m_width, m_height,
                   m_dc, 0, 0, SRCCOPY);
            ReleaseDC(m_hwnd, screen);
        }
    }
}
