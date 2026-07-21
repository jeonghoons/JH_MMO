#pragma once
#include <gl/gl.h>
#include <gl/glu.h>

class NetworkManager;

class Renderer {
public:
    Renderer(NetworkManager* netMgr);
    ~Renderer();

    bool Initialize(HWND hwnd);
    void Render();
    void Resize(int width, int height);

private:
    void BuildFont();
    void KillFont();
    void PrintText(float x, float y, const char* fmt, ...);

    NetworkManager* _netMgr;
    HDC _hDC;
    HGLRC _hRC;
    GLuint _fontBase;
};

