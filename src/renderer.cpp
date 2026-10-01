#include "renderer.h"
#include <math.h>

static const float PI = 3.14159265358979323846f;
static const float FOV = 70.0f * PI / 180.0f;
static const float NEAR_Z = 0.10f;
static const float FAR_Z = 64.0f;

/* Deliberately small for the S730. Increase after profiling. */
static const int TERRAIN_RADIUS = 9;
static const int TERRAIN_RADIUS2 = TERRAIN_RADIUS * TERRAIN_RADIUS;

/* 12.12 is enough for our 240x320 framebuffer and keeps edge math in 32-bit. */
static const int FP_SHIFT = 12;
static const int FP_ONE = 1 << FP_SHIFT;

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
      m_projScaleX(1.0f), m_projScaleY(1.0f),
      m_cachedYaw(0.0f), m_cachedPitch(0.0f),
      m_cameraCacheValid(false)
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
    int fx0 = (int)(x0 * (float)FP_ONE);
    int fy0 = (int)(y0 * (float)FP_ONE);
    int fx1 = (int)(x1 * (float)FP_ONE);
    int fy1 = (int)(y1 * (float)FP_ONE);
    int fx2 = (int)(x2 * (float)FP_ONE);
    int fy2 = (int)(y2 * (float)FP_ONE);

    int minX, maxX, minY, maxY;
    long area;
    long e0row, e1row, e2row;
    long e0step, e1step, e2step;
    long e0down, e1down, e2down;
    float dzdx, dzdy, zrow;
    int z0i, z1i, z2i;
    int x, y;
    DWORD* pixels = (DWORD*)m_pixels;
    unsigned short* depth = m_depth;

    /* Integer bounding box: no floor()/ceil() in the hot path. */
    minX = fx0;
    maxX = fx0;
    minY = fy0;
    maxY = fy0;
    if (fx1 < minX) minX = fx1;
    if (fx2 < minX) minX = fx2;
    if (fx1 > maxX) maxX = fx1;
    if (fx2 > maxX) maxX = fx2;
    if (fy1 < minY) minY = fy1;
    if (fy2 < minY) minY = fy2;
    if (fy1 > maxY) maxY = fy1;
    if (fy2 > maxY) maxY = fy2;

    /* Convert fixed-point bounds to pixel bounds. */
    minX = minX >> FP_SHIFT;
    maxX = (maxX + FP_ONE - 1) >> FP_SHIFT;
    minY = minY >> FP_SHIFT;
    maxY = (maxY + FP_ONE - 1) >> FP_SHIFT;

    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= m_width) maxX = m_width - 1;
    if (maxY >= m_height) maxY = m_height - 1;

    if (minX > maxX || minY > maxY)
        return;

    /*
     * Edge functions are kept in 12.12 fixed point.
     * The per-pixel loop now uses only integer additions/comparisons
     * for coverage instead of three floating-point edge equations.
     */
    area = (long)(fx1 - fx0) * (long)(fy2 - fy0) -
           (long)(fy1 - fy0) * (long)(fx2 - fx0);

    if (area > -1 || area < 1)
        return;

    e0step = -(long)(fy1 - fy0);
    e1step = -(long)(fy2 - fy1);
    e2step = -(long)(fy0 - fy2);

    e0down = (long)(fx1 - fx0);
    e1down = (long)(fx2 - fx1);
    e2down = (long)(fx0 - fx2);

    /*
     * Evaluate edges at the first pixel center. Using fixed-point
     * coordinates avoids repeated float arithmetic in every pixel.
     */
    {
        long px = ((long)minX << FP_SHIFT) + (FP_ONE >> 1);
        long py = ((long)minY << FP_SHIFT) + (FP_ONE >> 1);

        e0row = e0step * (px - fx0) + e0down * (py - fy0);
        e1row = e1step * (px - fx1) + e1down * (py - fy1);
        e2row = e2step * (px - fx2) + e2down * (py - fy2);
    }

    if (area < 0) {
        e0row = -e0row;
        e1row = -e1row;
        e2row = -e2row;
        e0step = -e0step;
        e1step = -e1step;
        e2step = -e2step;
        e0down = -e0down;
        e1down = -e1down;
        e2down = -e2down;
        area = -area;
    }

    z0i = ClampInt((int)((z0 / FAR_Z) * 65534.0f), 0, 65534);
    z1i = ClampInt((int)((z1 / FAR_Z) * 65534.0f), 0, 65534);
    z2i = ClampInt((int)((z2 / FAR_Z) * 65534.0f), 0, 65534);

    /* Depth setup is done once per triangle; pixels only add dzdx. */
    {
        float screenArea = (x1 - x0) * (y2 - y0) -
                           (y1 - y0) * (x2 - x0);

        dzdx = ((float)(z1i - z0i) * (y2 - y0) -
                (float)(z2i - z0i) * (y1 - y0)) / screenArea;

        dzdy = ((x1 - x0) * (float)(z2i - z0i) -
                (x2 - x0) * (float)(z1i - z0i)) / screenArea;

        zrow = (float)z0i +
               dzdx * ((float)minX + 0.5f - x0) +
               dzdy * ((float)minY + 0.5f - y0);
    }

    for (y = minY; y <= maxY; ++y) {
        long e0 = e0row;
        long e1 = e1row;
        long e2 = e2row;
        float zcur = zrow;
        int index = y * m_width + minX;

        for (x = minX; x <= maxX; ++x) {
            if (e0 >= 0 && e1 >= 0 && e2 >= 0) {
                int zz = (int)zcur;
                if (zz < 0) zz = 0;
                if (zz > 65534) zz = 65534;

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

            /* Skip the square's corners: a circular view covers fewer columns. */
            {
                int dx = x - cx;
                int dz = z - cz;
                if (dx * dx + dz * dz > TERRAIN_RADIUS2)
                    continue;
            }

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
    if (!m_cameraCacheValid || c.yaw != m_cachedYaw || c.pitch != m_cachedPitch) {
        m_camCosYaw = (float)cos(c.yaw);
        m_camSinYaw = (float)sin(c.yaw);
        m_camCosPitch = (float)cos(c.pitch);
        m_camSinPitch = (float)sin(c.pitch);
        m_cachedYaw = c.yaw;
        m_cachedPitch = c.pitch;
        m_cameraCacheValid = true;
    }

    /* FOV/aspect never change after Initialize(), so do this only once. */
    if (m_projScaleX == 1.0f && m_projScaleY == 1.0f) {
        halfFovTan = (float)tan(FOV * 0.5f);
        aspect = (float)m_width / (float)m_height;
        m_projScaleX = ((float)m_width * 0.5f) / (halfFovTan * aspect);
        m_projScaleY = ((float)m_height * 0.5f) / halfFovTan;
    }

    Clear(Color(115, 185, 235));
    DrawTerrain(t, c);

    screen = GetDC(m_hwnd);
    if (screen) {
        BitBlt(screen, 0, 0, m_width, m_height,
               m_dc, 0, 0, SRCCOPY);
        ReleaseDC(m_hwnd, screen);
    }
}
