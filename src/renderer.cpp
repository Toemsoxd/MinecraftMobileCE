#include "renderer.h"
#include <math.h>

/*
 * MinecraftMobileCE software renderer v2
 *
 * Design goals for Windows Mobile / Windows CE class hardware:
 * - 160x120 internal framebuffer (2x presentation to 320x240)
 * - RGB565 pixels
 * - no GDI drawing during 3D rendering
 * - float only for the small vertex-transform stage
 * - integer/fixed-point triangle rasterizer
 * - 16-bit depth buffer
 * - tiny terrain working set
 * - no per-frame heap allocations
 *
 * The DIB is only the presentation surface. All 3D work is performed
 * by this software rasterizer.
 */

static const float PI = 3.14159265358979323846f;
static const float FOV = 70.0f * PI / 180.0f;
static const float NEAR_Z = 0.08f;
static const float FAR_Z = 64.0f;

static const int TERRAIN_RADIUS = 8;
static const int TERRAIN_RADIUS2 = TERRAIN_RADIUS * TERRAIN_RADIUS;

static const int FP_SHIFT = 12;
static const int FP_ONE = 1 << FP_SHIFT;
static const int DEPTH_MAX = 65535;

typedef unsigned short Pixel;

struct Vertex {
    float x;
    float y;
    float z;
};

static Pixel RGB565(int r, int g, int b)
{
    return (Pixel)(((r >> 3) << 11) |
                   ((g >> 2) << 5) |
                   (b >> 3));
}

static int ClampInt(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
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
      m_pixels(0), m_width(160), m_height(120), m_depth(0),
      m_camCosYaw(1.0f), m_camSinYaw(0.0f),
      m_camCosPitch(1.0f), m_camSinPitch(0.0f),
      m_projScaleX(1.0f), m_projScaleY(1.0f),
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

    /*
     * The game is intentionally rendered at half resolution.
     * Keep the internal size deterministic: this is the performance
     * target for the S730-class device.
     */
    m_width = width / 2;
    m_height = height / 2;

    if (m_width < 1) m_width = 160;
    if (m_height < 1) m_height = 120;

    m_cameraCacheValid = false;

    return CreateFramebuffer();
}

bool Renderer::CreateFramebuffer()
{
    BYTE infoBuffer[sizeof(BITMAPINFO) + 2 * sizeof(DWORD)];
    BITMAPINFO* bi;
    DWORD* masks;
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

    /*
     * Windows CE requires BI_BITFIELDS for 16-bit non-palettized DIBs,
     * with three color masks. RGB565 is native to our software buffer.
     */
    bi = (BITMAPINFO*)infoBuffer;
    ZeroMemory(infoBuffer, sizeof(infoBuffer));

    bi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi->bmiHeader.biWidth = m_width;
    bi->bmiHeader.biHeight = -m_height;
    bi->bmiHeader.biPlanes = 1;
    bi->bmiHeader.biBitCount = 16;
    bi->bmiHeader.biCompression = BI_BITFIELDS;

    masks = (DWORD*)bi->bmiColors;
    masks[0] = 0xF800;
    masks[1] = 0x07E0;
    masks[2] = 0x001F;

    m_bitmap = CreateDIBSection(m_dc, bi, DIB_RGB_COLORS,
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

    Clear(RGB565(115, 185, 235));
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

void Renderer::Clear(Pixel color)
{
    Pixel* pixels;
    int count;
    int i;

    if (!m_pixels || !m_depth)
        return;

    pixels = (Pixel*)m_pixels;
    count = m_width * m_height;

    for (i = 0; i < count; ++i) {
        pixels[i] = color;
        m_depth[i] = DEPTH_MAX;
    }
}

Vertex Renderer::WorldToView(float x, float y, float z,
                             const Camera& c) const
{
    Vertex v;
    float dx = x - c.x;
    float dy = y - c.y;
    float dz = z - c.z;

    float xz = dx * m_camCosYaw - dz * m_camSinYaw;
    float zz = dx * m_camSinYaw + dz * m_camCosYaw;

    v.x = xz;
    v.y = dy * m_camCosPitch + zz * m_camSinPitch;
    v.z = -dy * m_camSinPitch + zz * m_camCosPitch;

    return v;
}

bool Renderer::Project(const Vertex& v, float* sx, float* sy) const
{
    if (v.z <= NEAR_Z || v.z > FAR_Z)
        return false;

    *sx = (float)m_width * 0.5f +
          v.x * m_projScaleX / v.z;

    *sy = (float)m_height * 0.5f -
          v.y * m_projScaleY / v.z;

    return true;
}

static Vertex ClipNear(const Vertex& a, const Vertex& b)
{
    Vertex v;
    float t = (NEAR_Z - a.z) / (b.z - a.z);

    v.x = a.x + (b.x - a.x) * t;
    v.y = a.y + (b.y - a.y) * t;
    v.z = NEAR_Z;
    return v;
}

/*
 * Clip one triangle against the near plane. Far clipping is unnecessary
 * for normal terrain because the terrain working radius is small; triangles
 * beyond FAR_Z are rejected before rasterization.
 */
void Renderer::DrawViewTriangle(Vertex a, Vertex b, Vertex c, Pixel color)
{
    bool ia = a.z >= NEAR_Z;
    bool ib = b.z >= NEAR_Z;
    bool ic = c.z >= NEAR_Z;
    int inside = (ia ? 1 : 0) + (ib ? 1 : 0) + (ic ? 1 : 0);

    if (inside == 0)
        return;

    if (inside == 3) {
        float x0, y0, x1, y1, x2, y2;

        if (!Project(a, &x0, &y0) ||
            !Project(b, &x1, &y1) ||
            !Project(c, &x2, &y2))
            return;

        DrawTriangle(x0, y0, a.z,
                     x1, y1, b.z,
                     x2, y2, c.z,
                     color);
        return;
    }

    /*
     * A triangle clipped by one plane becomes either one or two triangles.
     * The cases below avoid a general-purpose polygon allocator.
     */
    if (ia && ib && !ic) {
        Vertex ac = ClipNear(a, c);
        Vertex bc = ClipNear(b, c);
        DrawViewTriangle(a, b, ac, color);
        DrawViewTriangle(ac, b, bc, color);
        return;
    }

    if (ia && !ib && ic) {
        Vertex ab = ClipNear(a, b);
        Vertex bc = ClipNear(b, c);
        DrawViewTriangle(a, ab, c, color);
        DrawViewTriangle(ab, bc, c, color);
        return;
    }

    if (!ia && ib && ic) {
        Vertex ab = ClipNear(a, b);
        Vertex ac = ClipNear(a, c);
        DrawViewTriangle(ab, b, c, color);
        DrawViewTriangle(ab, c, ac, color);
        return;
    }
}

void Renderer::DrawTriangle(float x0, float y0, float z0,
                            float x1, float y1, float z1,
                            float x2, float y2, float z2,
                            Pixel color)
{
    int fx0 = (int)(x0 * (float)FP_ONE);
    int fy0 = (int)(y0 * (float)FP_ONE);
    int fx1 = (int)(x1 * (float)FP_ONE);
    int fy1 = (int)(y1 * (float)FP_ONE);
    int fx2 = (int)(x2 * (float)FP_ONE);
    int fy2 = (int)(y2 * (float)FP_ONE);

    long area;
    long e0, e1, e2;
    long e0dx, e1dx, e2dx;
    long e0dy, e1dy, e2dy;

    int minX, maxX, minY, maxY;
    int x, y;

    Pixel* pixels = (Pixel*)m_pixels;
    unsigned short* depth = m_depth;

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

    minX >>= FP_SHIFT;
    minY >>= FP_SHIFT;
    maxX = (maxX + FP_ONE - 1) >> FP_SHIFT;
    maxY = (maxY + FP_ONE - 1) >> FP_SHIFT;

    if (maxX < 0 || maxY < 0 ||
        minX >= m_width || minY >= m_height)
        return;

    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= m_width) maxX = m_width - 1;
    if (maxY >= m_height) maxY = m_height - 1;

    if (minX > maxX || minY > maxY)
        return;

    /*
     * Screen-space backface culling. This removes triangles facing away
     * from the camera before entering the expensive pixel loop.
     */
    area = (long)(fx1 - fx0) * (long)(fy2 - fy0) -
           (long)(fy1 - fy0) * (long)(fx2 - fx0);

    if (area <= 0)
        return;

    /*
     * Edge functions. One increment per pixel; no multiplication inside
     * the inner raster loop.
     */
    e0dx = -(long)(fy1 - fy0);
    e1dx = -(long)(fy2 - fy1);
    e2dx = -(long)(fy0 - fy2);

    e0dy = (long)(fx1 - fx0);
    e1dy = (long)(fx2 - fx1);
    e2dy = (long)(fx0 - fx2);

    {
        long px = ((long)minX << FP_SHIFT) + (FP_ONE >> 1);
        long py = ((long)minY << FP_SHIFT) + (FP_ONE >> 1);

        e0 = e0dx * (px - fx0) + e0dy * (py - fy0);
        e1 = e1dx * (px - fx1) + e1dy * (py - fy1);
        e2 = e2dx * (px - fx2) + e2dy * (py - fy2);
    }

    /*
     * Depth interpolation is affine in screen space. At this low internal
     * resolution this is sufficient and avoids a reciprocal per pixel.
     */
    {
        int z0i = ClampInt((int)(z0 * (65534.0f / FAR_Z)), 0, 65534);
        int z1i = ClampInt((int)(z1 * (65534.0f / FAR_Z)), 0, 65534);
        int z2i = ClampInt((int)(z2 * (65534.0f / FAR_Z)), 0, 65534);

        long dzdxFixed;
        long dzdyFixed;
        long zFixed;
        long det = area;

        /*
         * z is kept in 16.16 fixed point for the pixel loop.
         * Compute derivatives using the same screen-space determinant.
         */
        dzdxFixed =
            (((long)(z1i - z0i) * (long)(fy2 - fy0) -
              (long)(z2i - z0i) * (long)(fy1 - fy0)) << FP_SHIFT)
            / det;

        dzdyFixed =
            (((long)(fx1 - fx0) * (long)(z2i - z0i) -
              (long)(fx2 - fx0) * (long)(z1i - z0i)) << FP_SHIFT)
            / det;

        {
            long px = ((long)minX << FP_SHIFT) + (FP_ONE >> 1);
            long py = ((long)minY << FP_SHIFT) + (FP_ONE >> 1);

            zFixed = ((long)z0i << FP_SHIFT) +
                     dzdxFixed * (px - fx0) / FP_ONE +
                     dzdyFixed * (py - fy0) / FP_ONE;
        }

        for (y = minY; y <= maxY; ++y) {
            long rowE0 = e0;
            long rowE1 = e1;
            long rowE2 = e2;
            long rowZ = zFixed;
            int index = y * m_width + minX;

            for (x = minX; x <= maxX; ++x) {
                if (rowE0 >= 0 && rowE1 >= 0 && rowE2 >= 0) {
                    int z = (int)(rowZ >> FP_SHIFT);

                    if (z < 0) z = 0;
                    if (z >= DEPTH_MAX) z = DEPTH_MAX - 1;

                    if (z < (int)depth[index]) {
                        depth[index] = (unsigned short)z;
                        pixels[index] = color;
                    }
                }

                rowE0 += e0dx;
                rowE1 += e1dx;
                rowE2 += e2dx;
                rowZ += dzdxFixed;
                ++index;
            }

            e0 += e0dy;
            e1 += e1dy;
            e2 += e2dy;
            zFixed += dzdyFixed;
        }
    }
}

void Renderer::DrawQuad(const Vertex& a, const Vertex& b,
                        const Vertex& c, const Vertex& d,
                        Pixel color)
{
    DrawViewTriangle(a, b, c, color);
    DrawViewTriangle(a, c, d, color);
}

void Renderer::DrawTerrain(const Terrain& terrain, const Camera& camera)
{
    int cx = (int)camera.x;
    int cz = (int)camera.z;
    int z;

    Pixel topColor = RGB565(79, 171, 69);
    Pixel westColor = RGB565(94, 69, 43);
    Pixel eastColor = RGB565(79, 59, 41);
    Pixel northColor = RGB565(69, 51, 36);
    Pixel southColor = RGB565(86, 63, 40);

    for (z = cz - TERRAIN_RADIUS; z <= cz + TERRAIN_RADIUS; ++z) {
        int x;

        if (z < 0 || z >= WORLD_SIZE)
            continue;

        for (x = cx - TERRAIN_RADIUS; x <= cx + TERRAIN_RADIUS; ++x) {
            int h;
            int left, right, north, south;
            int dx, dz;

            if (x < 0 || x >= WORLD_SIZE)
                continue;

            dx = x - cx;
            dz = z - cz;

            if (dx * dx + dz * dz > TERRAIN_RADIUS2)
                continue;

            h = terrain.GetHeight(x, z);

            left  = (x > 0) ? terrain.GetHeight(x - 1, z) : h;
            right = (x < WORLD_SIZE - 1) ?
                    terrain.GetHeight(x + 1, z) : h;
            north = (z > 0) ? terrain.GetHeight(x, z - 1) : h;
            south = (z < WORLD_SIZE - 1) ?
                    terrain.GetHeight(x, z + 1) : h;

            /*
             * Top.
             */
            DrawQuad(
                WorldToView((float)x,     (float)h, (float)z,     camera),
                WorldToView((float)x + 1, (float)h, (float)z,     camera),
                WorldToView((float)x + 1, (float)h, (float)z + 1, camera),
                WorldToView((float)x,     (float)h, (float)z + 1, camera),
                topColor
            );

            /*
             * Only exposed side walls are generated.
             * This is the main geometry reduction for the heightmap.
             */
            if (left < h) {
                DrawQuad(
                    WorldToView((float)x, (float)left, (float)z + 1, camera),
                    WorldToView((float)x, (float)h,    (float)z + 1, camera),
                    WorldToView((float)x, (float)h,    (float)z,     camera),
                    WorldToView((float)x, (float)left, (float)z,     camera),
                    westColor
                );
            }

            if (right < h) {
                DrawQuad(
                    WorldToView((float)x + 1, (float)right, (float)z,     camera),
                    WorldToView((float)x + 1, (float)h,     (float)z,     camera),
                    WorldToView((float)x + 1, (float)h,     (float)z + 1, camera),
                    WorldToView((float)x + 1, (float)right, (float)z + 1, camera),
                    eastColor
                );
            }

            if (north < h) {
                DrawQuad(
                    WorldToView((float)x + 1, (float)north, (float)z, camera),
                    WorldToView((float)x + 1, (float)h,     (float)z, camera),
                    WorldToView((float)x,     (float)h,     (float)z, camera),
                    WorldToView((float)x,     (float)north, (float)z, camera),
                    northColor
                );
            }

            if (south < h) {
                DrawQuad(
                    WorldToView((float)x,     (float)south, (float)z + 1, camera),
                    WorldToView((float)x,     (float)h,     (float)z + 1, camera),
                    WorldToView((float)x + 1, (float)h,     (float)z + 1, camera),
                    WorldToView((float)x + 1, (float)south, (float)z + 1, camera),
                    southColor
                );
            }
        }
    }
}

void Renderer::Render(const Terrain& terrain, const Camera& camera)
{
    HDC screen;

    if (!m_dc || !m_pixels || !m_depth)
        return;

    if (!m_cameraCacheValid ||
        camera.yaw != m_cachedYaw ||
        camera.pitch != m_cachedPitch) {

        m_camCosYaw = (float)cos(camera.yaw);
        m_camSinYaw = (float)sin(camera.yaw);
        m_camCosPitch = (float)cos(camera.pitch);
        m_camSinPitch = (float)sin(camera.pitch);

        m_cachedYaw = camera.yaw;
        m_cachedPitch = camera.pitch;
        m_cameraCacheValid = true;
    }

    /*
     * Projection constants never change after initialization.
     * Vertical FOV is used so the 4:3 S730 viewport stays stable.
     */
    if (m_projScaleX == 1.0f && m_projScaleY == 1.0f) {
        float halfTan = (float)tan(FOV * 0.5f);

        m_projScaleY = ((float)m_height * 0.5f) / halfTan;
        m_projScaleX = m_projScaleY;
    }

    Clear(RGB565(115, 185, 235));
    DrawTerrain(terrain, camera);

    /*
     * GDI is used only to present our completed software framebuffer.
     * It does not participate in 3D rendering.
     */
    screen = GetDC(m_hwnd);
    if (screen) {
        StretchBlt(screen,
                   0, 0, m_width * 2, m_height * 2,
                   m_dc,
                   0, 0, m_width, m_height,
                   SRCCOPY);
        ReleaseDC(m_hwnd, screen);
    }
}
