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
    int X0, Y0, X1, Y1, X2, Y2;
    int minX, maxX, minY, maxY;
    long long area;
    long long A0, B0, C0;
    long long A1, B1, C1;
    long long A2, B2, C2;
    float dzdx, dzdy;
    int Z0, Z1, Z2;
    int zStart;
    int dzdxI, dzdyI;
    int x, y;
    DWORD* pixels;
    unsigned short* depth;

    X0 = (int)(x0 * (float)FP_ONE);
    Y0 = (int)(y0 * (float)FP_ONE);
    X1 = (int)(x1 * (float)FP_ONE);
    Y1 = (int)(y1 * (float)FP_ONE);
    X2 = (int)(x2 * (float)FP_ONE);
    Y2 = (int)(y2 * (float)FP_ONE);

    minX = ClampInt((int)floor(x0), 0, m_width - 1);
    maxX = ClampInt((int)ceil(x0 > x1 ? (x0 > x2 ? x0 : x2) :
                                      (x1 > x2 ? x1 : x2)), 0, m_width - 1);
    minY = ClampInt((int)floor(y0 < y1 ? (y0 < y2 ? y0 : y2) :
                                      (y1 < y2 ? y1 : y2)), 0, m_height - 1);
    maxY = ClampInt((int)ceil(y0 > y1 ? (y0 > y2 ? y0 : y2) :
                                      (y1 > y2 ? y1 : y2)), 0, m_height - 1);

    if (minX > maxX || minY > maxY)
        return;

    area = (long long)(X1 - X0) * (long long)(Y2 - Y0) -
           (long long)(Y1 - Y0) * (long long)(X2 - X0);

    if (area == 0)
        return;

    /*
     * Edge equations. The sign of area is retained, so both triangle
     * windings work without an expensive normalization step.
     */
    A0 = (long long)(Y0 - Y1);
    B0 = (long long)(X1 - X0);
    C0 = -A0 * X0 - B0 * Y0;

    A1 = (long long)(Y1 - Y2);
    B1 = (long long)(X2 - X1);
    C1 = -A1 * X1 - B1 * Y1;

    A2 = (long long)(Y2 - Y0);
    B2 = (long long)(X0 - X2);
    C2 = -A2 * X2 - B2 * Y2;

    /* Normalize all edges to the same inside-test orientation. */
    if (area < 0) {
        A0 = -A0; B0 = -B0; C0 = -C0;
        A1 = -A1; B1 = -B1; C1 = -C1;
        A2 = -A2; B2 = -B2; C2 = -C2;
        area = -area;
    }

    Z0 = ClampInt((int)((z0 / FAR_Z) * 65534.0f), 0, 65534);
    Z1 = ClampInt((int)((z1 / FAR_Z) * 65534.0f), 0, 65534);
    Z2 = ClampInt((int)((z2 / FAR_Z) * 65534.0f), 0, 65534);

    /*
     * Depth plane:
     * z(x,y) = z0 + dzdx*x + dzdy*y
     * Calculate its slopes once per triangle, then use integer adds.
     */
    dzdx = ((float)(Z1 - Z0) * (float)(Y2 - Y0) -
            (float)(Z2 - Z0) * (float)(Y1 - Y0)) / (float)area;
    dzdy = ((float)(X1 - X0) * (float)(Z2 - Z0) -
            (float)(X2 - X0) * (float)(Z1 - Z0)) / (float)area;

    dzdxI = (int)(dzdx * (float)FP_ONE);
    dzdyI = (int)(dzdy * (float)FP_ONE);

    /*
     * Evaluate at pixel centers. Edge values use 16.16 coordinates,
     * while the per-pixel step is simply A*65536 or B*65536.
     */
    {
        long long fx = ((long long)minX << FP_SHIFT) + (FP_ONE >> 1);
        long long fy = ((long long)minY << FP_SHIFT) + (FP_ONE >> 1);
        long long e0row = A0 * fx + B0 * fy + C0;
        long long e1row = A1 * fx + B1 * fy + C1;
        long long e2row = A2 * fx + B2 * fy + C2;
        int rowZ = Z0 + (int)(((long long)dzdxI * (minX * FP_ONE - X0) +
                               (long long)dzdyI * (minY * FP_ONE - Y0)) >> FP_SHIFT);

        pixels = (DWORD*)m_pixels;
        depth = m_depth;

        for (y = minY; y <= maxY; ++y) {
            long long e0 = e0row;
            long long e1 = e1row;
            long long e2 = e2row;
            int zcur = rowZ;
            int index = y * m_width + minX;

            for (x = minX; x <= maxX; ++x) {
                if (e0 >= 0 && e1 >= 0 && e2 >= 0) {
                    int zz = ClampInt(zcur >> FP_SHIFT, 0, 65534);
                    if (zz < (int)depth[index]) {
                        depth[index] = (unsigned short)zz;
                        pixels[index] = color;
                    }
                }

                e0 += A0 * FP_ONE;
                e1 += A1 * FP_ONE;
                e2 += A2 * FP_ONE;
                zcur += dzdxI;
                ++index;
            }

            e0row += B0 * FP_ONE;
            e1row += B1 * FP_ONE;
            e2row += B2 * FP_ONE;
            rowZ += dzdyI;
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
