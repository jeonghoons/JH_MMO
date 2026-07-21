#include "pch.h"
#include "Renderer.h"
#include "NetworkManager.h"
#include <stdio.h>

Renderer::Renderer(NetworkManager* netMgr) : _netMgr(netMgr), _hDC(NULL), _hRC(NULL), _fontBase(0) {}

Renderer::~Renderer() {
    KillFont();
    if (_hRC) { wglMakeCurrent(NULL, NULL); wglDeleteContext(_hRC); }
    if (_hDC) ReleaseDC(g_hWnd, _hDC);
}

bool Renderer::Initialize(HWND hwnd) {
    g_hWnd = hwnd;
    _hDC = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR), 1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA,
        16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, PFD_MAIN_PLANE, 0, 0, 0, 0
    };

    int pixelFormat = ChoosePixelFormat(_hDC, &pfd);
    SetPixelFormat(_hDC, pixelFormat, &pfd);
    _hRC = wglCreateContext(_hDC);
    wglMakeCurrent(_hDC, _hRC);

    glShadeModel(GL_SMOOTH);
    glClearColor(0.0f, 0.0f, 0.0f, 0.5f);
    glClearDepth(1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    BuildFont();
    return true;
}

void Renderer::Resize(int width, int height) {
    if (height == 0) height = 1;
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0f, (GLfloat)width / (GLfloat)height, 0.1f, 100.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void Renderer::Render() {
    int size = 0;
    float* points = nullptr;
    _netMgr->GetPointData(&size, &points);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -3.5f);

    glColor3f(1, 1, 0);
    PrintText(-1.8f, 1.2f, "STRESS TEST [%d]", active_clients.load());
    PrintText(-1.8f, 1.05f, "Delay : %dms", global_delay.load());

    glColor3f(1, 1, 1);
    glPointSize(1.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < size; i++) {
        glVertex3f(points[i * 2] / 7500.0f, points[i * 2 + 1] / 7500.0f, 0.0f);
    }
    glEnd();
    SwapBuffers(_hDC);
}

void Renderer::BuildFont() {
    _fontBase = glGenLists(96);
    HFONT font = CreateFont(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        FF_DONTCARE | DEFAULT_PITCH, L"Courier New");
    HFONT oldfont = (HFONT)SelectObject(_hDC, font);
    wglUseFontBitmaps(_hDC, 32, 96, _fontBase);
    SelectObject(_hDC, oldfont);
    DeleteObject(font);
}

void Renderer::KillFont() { glDeleteLists(_fontBase, 96); }

void Renderer::PrintText(float x, float y, const char* fmt, ...) {
    char text[256];
    va_list ap;
    va_start(ap, fmt);
    vsprintf_s(text, fmt, ap);
    va_end(ap);

    glRasterPos2f(x, y);
    glPushAttrib(GL_LIST_BIT);
    glListBase(_fontBase - 32);
    glCallLists((GLsizei)strlen(text), GL_UNSIGNED_BYTE, text);
    glPopAttrib();
}