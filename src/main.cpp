#include <windows.h>
#include "terrain.h"
#include "renderer.h"

static Renderer g_renderer;
static Terrain g_terrain;
static Camera g_camera;

static bool Down(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static void Update(float dt)
{
    const float speed = 12.0f;
    const float turn = 1.8f;

    if (Down('A')) g_camera.x -= speed * dt;
    if (Down('D')) g_camera.x += speed * dt;
    if (Down('W')) g_camera.z += speed * dt;
    if (Down('S')) g_camera.z -= speed * dt;
    if (Down(VK_SPACE)) g_camera.y += speed * dt;

    if (Down(VK_LEFT)) g_camera.yaw -= turn * dt;
    if (Down(VK_RIGHT)) g_camera.yaw += turn * dt;
    if (Down(VK_UP)) g_camera.pitch -= turn * dt;
    if (Down(VK_DOWN)) g_camera.pitch += turn * dt;

    if (g_camera.pitch > 1.45f) g_camera.pitch = 1.45f;
    if (g_camera.pitch < -1.45f) g_camera.pitch = -1.45f;

    if (g_camera.x < 1) g_camera.x = 1;
    if (g_camera.z < 1) g_camera.z = 1;
    if (g_camera.x > WORLD_SIZE - 2) g_camera.x = WORLD_SIZE - 2;
    if (g_camera.z > WORLD_SIZE - 2) g_camera.z = WORLD_SIZE - 2;
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
                        WS_POPUP, 0, 0, 240, 320,
                        0, 0, hi, 0);

    if (!hwnd)
        return 0;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    g_terrain.Generate(0xCE042001UL);

    g_camera.x = 128;
    g_camera.y = 40;
    g_camera.z = 128;
    g_camera.yaw = 0;
    g_camera.pitch = -0.25f;

    if (!g_renderer.Initialize(hwnd, 240, 320)) {
        MessageBox(hwnd,
                   TEXT("DirectDraw initialization failed."),
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
