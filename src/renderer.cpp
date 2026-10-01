#include "renderer.h"
#include <math.h>

static const float PI = 3.14159265358979323846f;
static const float FOV = 70.0f * PI / 180.0f;
static const float NEAR_Z = 0.10f;
static const float FAR_Z = 64.0f;

/* Deliberately small for the S730. Increase after profiling. */
static const int TERRAIN_RADIUS = 11;
static const int FP_SHIFT = 16;
static const int FP_ONE = 65536;

static DWORD Color(unsigned char r, unsigned char g, unsigned char b)
{
    /* BI_RGB DIBs store bytes as B,G,R,0 on little-endian CE. */
    return ((DWORD)b) | ((DWORD)g << 8) | ((DWORD)r << 16);
}

static int ClampInt(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

Renderer::Renderer()
    : m_hwnd(0), m_dc(0), m_bitmap(0), m_oldBitmap(0),
      m_pixels(0), m_width(240), m_height(320), m_depth(0),
      m_camCosYaw(1.0f), m_camSinYaw(0.0f),
      m_camCosPitch(1.0f), m_camSinPitch(0.0f),
      m_projScaleX(1.0f), m_projScaleY(1.0f)
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

/*
 * Fast triangle rasterizer:
 * - screen coordinates are converted to 16.16 fixed point once
 * - edge functions are incremented with integer additions per pixel
 * - depth is also incremented instead of being recomputed with barycentrics
 *
 * This removes the old per-pixel float divisions/multiplications.
 */
void Renderer::DrawTriangle(float x0, float y0, float z0,
                            float x1, float y1, float z1,
                            float x2, float y2, float z2,
                            DWORD color)
{
    float minxf, maxxf, minyf, maxyf;
    int minX, maxX, minY, maxY;
    float area;
    float e0row, e1row, e2row;
    float e0step, e1step, e2step;
    float e0down, e1down, e2down;
    float dzdx, dzdy;
    float zrow, zcur;
    int z0i;
    int z1i;
    int z2i;
    int x, y;
    DWORD* pixels;
    unsigned short* depth;

    minxf = x0;
    maxxf = x0;
    minyf = y0;
    maxyf = y0;

    if (x1 < minxf) minxf = x1;
    if (x2 < minxf) minxf = x2;
    if (x1 > maxxf) maxxf = x1;
    if (x2 > maxxf) maxxf = x2;

    if (y1 < minyf) minyf = y1;
    if (y2 < minyf) minyf = y2;
    if (y1 > maxyf) maxyf = y1;
    if (y2 > maxyf) maxyf = y2;

    minX = ClampInt((int)floor(minxf), 0, m_width - 1);
    maxX = ClampInt((int)ceil(maxxf), 0, m_width - 1);
    minY = ClampInt((int)floor(minyf), 0, m_height - 1);
    maxY = ClampInt((int)ceil(maxyf), 0, m_height - 1);

    if (minX > maxX || minY > maxY)
        return;

    area = (x1 - x0) * (y2 - y0) -
           (y1 - y0) * (x2 - x0);

    if (area > -0.001f && area < 0.001f)
        return;

    /*
     * Edge equations are calculated once per triangle.
     * After that every pixel only performs additions/comparisons.
     */
    e0step = -(y1 - y0);
    e1step = -(y2 - y1);
    e2step = -(y0 - y2);

    e0down = (x1 - x0);
    e1down = (x2 - x1);
    e2down = (x0 - x2);

    e0row = e0step * ((float)minX + 0.5f - x0) +
            e0down * ((float)minY + 0.5f - y0);

    e1row = e1step * ((float)minX + 0.5f - x1) +
            e1down * ((float)minY + 0.5f - y1);

    e2row = e2step * ((float)minX + 0.5f - x2) +
            e2down * ((float)minY + 0.5f - y2);

    /*
     * Make the inside test independent of triangle winding.
     */
    if (area < 0.0f) {
        e0row = -e0row;
        e1row = -e1row;
        e2row = -e2row;
        e0step = -e0step;
        e1step = -e1step;
        e2step = -e2step;
        e0down = -e0down;
        e1down = -e1down;
        e2down = -e2down;
    }

    z0i = ClampInt((int)((z0 / FAR_Z) * 65534.0f), 0, 65534);
    z1i = ClampInt((int)((z1 / FAR_Z) * 65534.0f), 0, 65534);
    z2i = ClampInt((int)((z2 / FAR_Z) * 65534.0f), 0, 65534);

    /*
     * Depth plane is calculated once. Per pixel only adds dzdx.
     */
    dzdx = ((float)(z1i - z0i) * (y2 - y0) -
            (float)(z2i - z0i) * (y1 - y0)) / area;

    dzdy = ((x1 - x0) * (float)(z2i - z0i) -
            (x2 - x0) * (float)(z1i - z0i)) / area;

    zrow = (float)z0i +
           dzdx * ((float)minX + 0.5f - x0) +
           dzdy * ((float)minY + 0.5f - y0);

    pixels = (DWORD*)m_pixels;
    depth = m_depth;

    for (y = minY; y <= maxY; ++y) {
        float e0 = e0row;
        float e1 = e1row;
        float e2 = e2row;
        zcur = zrow;
        int index = y * m_width + minX;

        for (x = minX; x <= maxX; ++x) {
            if (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f) {
                int zz = ClampInt((int)zcur, 0, 65534);

                if (zz < (int)depth[index]) {
                    depth[index] = (unsigned short)zz;
                    pixels[index] = color;
                }
            }

            e0 += e0step;
            e1 += e1step;
            e2 += e2step;
            zcur += dzdx;
            ++index;
        }

        e0row += e0down;
        e1row += e1down;
        e2row += e2down;
        zrow += dzdy;
    }
}

bool Renderer::Project(float x, float y, float z,
                       const Camera& c,
                       float* sx, float* sy, float* sz)
{
    float dx = x - c.x;
    float dy = y - c.y;
    float dz = z - c.z;
    float vx, vy, vz;
    float ux, uy, uz;

    /* Camera basis uses cached trig values. */
    vx = dx * m_camCosYaw - dz * m_camSinYaw;
    vz = dx * m_camSinYaw + dz * m_camCosYaw;
    vy = dy;

    ux = vx;
    uy = vy * m_camCosPitch + vz * m_camSinPitch;
    uz = -vy * m_camSinPitch + vz * m_camCosPitch;

    if (uz <= NEAR_Z || uz > FAR_Z)
        return false;

    *sx = (float)m_width * 0.5f + ux * m_projScaleX / uz;
    *sy = (float)m_height * 0.5f - uy * m_projScaleY / uz;
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
    int z;
    DWORD grass = Color(79, 171, 69);
    DWORD side1 = Color(94, 69, 43);
    DWORD side2 = Color(79, 59, 41);
    DWORD side3 = Color(69, 51, 36);

    for (z = cz - TERRAIN_RADIUS; z <= cz + TERRAIN_RADIUS; ++z) {
        int x;

        if (z < 0 || z >= WORLD_SIZE)
            continue;

        for (x = cx - TERRAIN_RADIUS; x <= cx + TERRAIN_RADIUS; ++x) {
            int h;
            int l, rr, f, b;

            if (x < 0 || x >= WORLD_SIZE)
                continue;

            h = t.GetHeight(x, z);
            l = (x > 0) ? t.GetHeight(x - 1, z) : h;
            rr = (x < WORLD_SIZE - 1) ? t.GetHeight(x + 1, z) : h;
            f = (z > 0) ? t.GetHeight(x, z - 1) : h;
            b = (z < WORLD_SIZE - 1) ? t.GetHeight(x, z + 1) : h;

            /* Top surface. */
            AddQuad((float)x, (float)h, (float)z,
                    (float)x + 1.0f, (float)h, (float)z,
                    (float)x + 1.0f, (float)h, (float)z + 1.0f,
                    (float)x, (float)h, (float)z + 1.0f,
                    grass, c);

            /*
             * Only draw side faces on the side where the camera actually
             * sits. This removes roughly half of the exposed wall work.
             */
            if (l < h && c.x <= (float)x)
                AddQuad((float)x, (float)l, (float)z,
                        (float)x, (float)h, (float)z,
                        (float)x, (float)h, (float)z + 1.0f,
                        (float)x, (float)l, (float)z + 1.0f,
                        side1, c);

            if (rr < h && c.x >= (float)(x + 1))
                AddQuad((float)x + 1.0f, (float)l, (float)z + 1.0f,
                        (float)x + 1.0f, (float)h, (float)z + 1.0f,
                        (float)x + 1.0f, (float)h, (float)z,
                        (float)x + 1.0f, (float)l, (float)z,
                        side2, c);

            if (f < h && c.z <= (float)z)
                AddQuad((float)x, (float)f, (float)z,
                        (float)x + 1.0f, (float)f, (float)z,
                        (float)x + 1.0f, (float)h, (float)z,
                        (float)x, (float)h, (float)z,
                        side3, c);

            if (b < h && c.z >= (float)(z + 1))
                AddQuad((float)x + 1.0f, (float)b, (float)z + 1.0f,
                        (float)x, (float)b, (float)z + 1.0f,
                        (float)x, (float)h, (float)z + 1.0f,
                        (float)x + 1.0f, (float)h, (float)z + 1.0f,
                        side1, c);
        }
    }
}

void Renderer::Render(const Terrain& t, const Camera& c)
{
    float halfFovTan;
    float aspect;
    HDC screen;

    if (!m_dc || !m_pixels)
        return;

    /* Calculate camera trig exactly once per frame. */
    m_camCosYaw = (float)cos(c.yaw);
    m_camSinYaw = (float)sin(c.yaw);
    m_camCosPitch = (float)cos(c.pitch);
    m_camSinPitch = (float)sin(c.pitch);

    halfFovTan = (float)tan(FOV * 0.5f);
    aspect = (float)m_width / (float)m_height;
    m_projScaleX = ((float)m_width * 0.5f) / (halfFovTan * aspect);
    m_projScaleY = ((float)m_height * 0.5f) / halfFovTan;

    Clear(Color(115, 185, 235));
    DrawTerrain(t, c);

    screen = GetDC(m_hwnd);
    if (screen) {
        BitBlt(screen, 0, 0, m_width, m_height,
               m_dc, 0, 0, SRCCOPY);
        ReleaseDC(m_hwnd, screen);
    }
}
