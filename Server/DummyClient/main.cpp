#include "pch.h"
#include "NetworkManager.h"
#include "Renderer.h"

NetworkManager g_NetworkManager;
Renderer* g_Renderer = nullptr;
bool g_active = true;

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_ACTIVATE: g_active = !HIWORD(wParam); return 0;
    case WM_CLOSE: PostQuitMessage(0); return 0;
    case WM_SIZE: if (g_Renderer) g_Renderer->Resize(LOWORD(lParam), HIWORD(lParam)); return 0;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc = { CS_HREDRAW | CS_VREDRAW | CS_OWNDC, WndProc, 0, 0, hInstance,
                    LoadIcon(NULL, IDI_WINLOGO), LoadCursor(NULL, IDC_ARROW),
                    NULL, NULL, L"OpenGL" };
    RegisterClass(&wc);

    // 1. 원하는 실제 렌더링 화면(클라이언트 영역) 크기 지정
    RECT windowRect = { 0, 0, 640, 480 };
    DWORD dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    DWORD dwExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;

    // 2. 타이틀바와 테두리 두께를 포함한 최종 윈도우 크기 계산
    AdjustWindowRectEx(&windowRect, dwStyle, FALSE, dwExStyle);
    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;

    // 3. (0, 0) 대신 CW_USEDEFAULT를 사용하여 화면 가장자리에 잘리지 않도록 안전한 위치에 생성
    HWND hWnd = CreateWindowEx(dwExStyle, L"OpenGL", L"Stress Test Client",
        dwStyle,
        CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
        NULL, NULL, hInstance, NULL);

    g_Renderer = new Renderer(&g_NetworkManager);
    if (!g_Renderer->Initialize(hWnd)) return 0;

    ShowWindow(hWnd, SW_SHOW);
    g_Renderer->Resize(640, 480);
    g_NetworkManager.Initialize();

    MSG msg;
    bool done = false;
    while (!done) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) done = true;
            else { TranslateMessage(&msg); DispatchMessage(&msg); }
        }
        else {
            if (g_active) g_Renderer->Render();
        }
    }

    g_NetworkManager.Shutdown();
    delete g_Renderer;
    DestroyWindow(hWnd);
    UnregisterClass(L"OpenGL", hInstance);

    return msg.wParam;
}

int main()
{
    return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOW);
}