#include <windows.h>
#include <math.h>
#include "terrain.h"
#include "renderer.h"

static Renderer g_renderer;
static Terrain g_terrain;
static Camera g_camera;

static int g_hotbar = 0;
static bool g_inventory = false;
static bool g_prevI = false;
static bool g_prevP = false;
static bool g_prevL = false;

static bool Down(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static bool Pressed(int vk, bool* previous)
{
    bool now = Down(vk);
    bool pressed = now && !(*previous);
    *previous = now;
    return pressed;
}

static void EditInFront(bool place)
{
    float fx = (float)sin(g_camera.yaw);
    float fz = (float)cos(g_camera.yaw);
    int tx = (int)(g_camera.x + fx * 2.0f);
    int tz = (int)(g_camera.z + fz * 2.0f);

    if (place)
        g_terrain.AddBlockColumn(tx, tz);
    else
        g_terrain.RemoveBlockColumn(tx, tz);
}

static bool g_prevSpace = false;
static float g_verticalVelocity = 0.0f;
static bool g_grounded = false;

static float GroundY()
{
    int x = (int)g_camera.x;
    int z = (int)g_camera.z;

    if (x < 0) x = 0;
    if (x >= WORLD_SIZE) x = WORLD_SIZE - 1;
    if (z < 0) z = 0;
    if (z >= WORLD_SIZE) z = WORLD_SIZE - 1;

    return (float)g_terrain.GetHeight(x, z) + 1.60f;
}

static void Update(float dt)
{
    const float speed = 5.0f;
    const float turn = 1.8f;
    const float gravity = 14.0f;
    const float jumpSpeed = 6.0f;
    const float eyeHeight = 1.60f;
    float forward = 0.0f;
    float strafe = 0.0f;
    float sy;
    float cy;

    if (Down('0')) forward += 1.0f;
    if (Down('*')) forward -= 1.0f;
    if (Down('4')) strafe += 1.0f;
    if (Down('A')) strafe -= 1.0f;

    sy = (float)sin(g_camera.yaw);
    cy = (float)cos(g_camera.yaw);

    if (forward != 0.0f || strafe != 0.0f) {
        float len = (float)sqrt(forward * forward + strafe * strafe);
        float f = forward / len;
        float s = strafe / len;
        float nx = g_camera.x + (sy * f + cy * s) * speed * dt;
        float nz = g_camera.z + (cy * f - sy * s) * speed * dt;
        int tx;
        int tz;
        float currentGround;
        float nextGround;

        if (nx < 1.0f) nx = 1.0f;
        if (nz < 1.0f) nz = 1.0f;
        if (nx > WORLD_SIZE - 2) nx = WORLD_SIZE - 2;
        if (nz > WORLD_SIZE - 2) nz = WORLD_SIZE - 2;

        tx = (int)nx;
        tz = (int)nz;
        currentGround = g_camera.y - eyeHeight;
        nextGround = (float)g_terrain.GetHeight(tx, tz);

        /* Allow normal one-block steps, but don't walk through cliffs. */
        if (nextGround <= currentGround + 1.05f) {
            g_camera.x = nx;
            g_camera.z = nz;
        }
    }

    if (Pressed(VK_SPACE, &g_prevSpace) && g_grounded)
        g_verticalVelocity = jumpSpeed;

    g_verticalVelocity -= gravity * dt;
    g_camera.y += g_verticalVelocity * dt;

    {
        float floorY = GroundY();

        if (g_camera.y <= floorY) {
            g_camera.y = floorY;
            g_verticalVelocity = 0.0f;
            g_grounded = true;
        } else {
            g_grounded = false;
        }
    }

    if (Down(VK_LEFT))  g_camera.yaw -= turn * dt;
    if (Down(VK_RIGHT)) g_camera.yaw += turn * dt;
    if (Down(VK_UP))    g_camera.pitch -= turn * dt;
    if (Down(VK_DOWN))  g_camera.pitch += turn * dt;

    if (Down('Q'))
        g_hotbar = (g_hotbar + 8) % 9;

    if (Down('O'))
        g_hotbar = (g_hotbar + 1) % 9;

    if (Pressed('I', &g_prevI))
        g_inventory = !g_inventory;

    if (Pressed('P', &g_prevP))
        EditInFront(false);

    if (Pressed('L', &g_prevL))
        EditInFront(true);

    if (g_camera.pitch > 1.45f) g_camera.pitch = 1.45f;
    if (g_camera.pitch < -1.45f) g_camera.pitch = -1.45f;
}


static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    if (m == WM_KEYDOWN && w == VK_ESCAPE) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPTSTR cmd, int show)
{
    WNDCLASS wc;
    HWND hwnd;
    DWORD last;
    MSG msg;

    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.lpszClassName = TEXT("MinecraftMobileCE");

    if (!RegisterClass(&wc))
        return 0;

    hwnd = CreateWindow(TEXT("MinecraftMobileCE"),
                        TEXT("MinecraftMobileCE"),
                        WS_POPUP, 0, 0, 320, 240,
                        0, 0, hi, 0);

    if (!hwnd)
        return 0;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    g_terrain.Generate(0xCE042001UL);

    g_camera.x = 128;
    g_camera.y = (float)g_terrain.GetHeight(128, 128) + 1.60f;
    g_camera.z = 128;
    g_camera.yaw = 0;
    g_camera.pitch = 0.65f;

    if (!g_renderer.Initialize(hwnd, 320, 240)) {
        MessageBox(hwnd,
                   TEXT("Software renderer initialization failed."),
                   TEXT("MinecraftMobileCE"),
                   MB_OK | MB_ICONERROR);
        DestroyWindow(hwnd);
        return 0;
    }

    last = GetTickCount();
    ZeroMemory(&msg, sizeof(msg));

    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        {
            DWORD now = GetTickCount();
            float dt = (float)(now - last) * 0.001f;
            last = now;

            if (dt > 0.1f)
                dt = 0.1f;

            Update(dt);
            g_renderer.Render(g_terrain, g_camera);
        }
    }

    g_renderer.Shutdown();
    return 0;
}
