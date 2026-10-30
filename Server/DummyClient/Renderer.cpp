#include "Renderer.h"
#include <gl/GL.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "opengl32.lib")

using namespace std;

bool Renderer::Initialize(HWND hwnd)
{
	_hwnd = hwnd;
	_hdc = GetDC(hwnd);

	PIXELFORMATDESCRIPTOR pfd{};
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.iLayerType = PFD_MAIN_PLANE;

	const int format = ChoosePixelFormat(_hdc, &pfd);
	if (format == 0 || !SetPixelFormat(_hdc, format, &pfd)) return false;
	_hrc = wglCreateContext(_hdc);
	if (!_hrc || !wglMakeCurrent(_hdc, _hrc)) return false;

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_POINT_SMOOTH);

	// HUD 글꼴: ASCII 32~127을 디스플레이 리스트로 만든다.
	_fontBase = glGenLists(96);
	HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS,
		CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE | FIXED_PITCH, L"Consolas");
	HGDIOBJ old = SelectObject(_hdc, font);
	wglUseFontBitmapsW(_hdc, 32, 96, _fontBase);
	SelectObject(_hdc, old);
	DeleteObject(font);
	return true;
}

void Renderer::Shutdown()
{
	if (_fontBase) glDeleteLists(_fontBase, 96);
	if (_hrc) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(_hrc); _hrc = nullptr; }
	if (_hdc) { ReleaseDC(_hwnd, _hdc); _hdc = nullptr; }
}

void Renderer::Resize(int width, int height)
{
	_width = max(1, width);
	_height = max(1, height);
	glViewport(0, 0, _width, _height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, _width, 0, _height, -1, 1);   // 픽셀 좌표, 원점은 왼쪽 아래
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void Renderer::WorldToScreen(float wx, float wy, float& sx, float& sy) const
{
	sx = _width * 0.5f + (wy - _camY) * _scale;
	sy = _height * 0.5f + (wx - _camX) * _scale;
}

void Renderer::ScreenToWorld(float sx, float sy, float& wx, float& wy) const
{
	wy = _camY + (sx - _width * 0.5f) / _scale;
	wx = _camX + (sy - _height * 0.5f) / _scale;
}

void Renderer::Zoom(int screenX, int screenY, float factor)
{
	_autoFit = false;
	// 커서 아래의 월드 지점이 확대 후에도 같은 자리에 있도록 카메라를 옮긴다.
	const float sy = static_cast<float>(_height - screenY);
	float wx, wy;
	ScreenToWorld(static_cast<float>(screenX), sy, wx, wy);
	_scale = clamp(_scale * factor, 0.001f, 5.f);
	_camY = wy - (screenX - _width * 0.5f) / _scale;
	_camX = wx - (sy - _height * 0.5f) / _scale;
}

void Renderer::Pan(int dxPixels, int dyPixels)
{
	_autoFit = false;
	_camY -= dxPixels / _scale;
	_camX += dyPixels / _scale;   // 창 좌표는 아래로 갈수록 y가 커진다
}

void Renderer::FitToContent(const vector<BotView>& bots, const vector<MonsterView>& monsters)
{
	float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
	auto add = [&](float x, float y) { minX = min(minX, x); maxX = max(maxX, x); minY = min(minY, y); maxY = max(maxY, y); };
	for (const auto& s : GMonsterSpawns) add(s.first, s.second);
	for (const BotView& b : bots) if (b.state != BotState::Closed) add(b.x, b.y);
	for (const MonsterView& m : monsters) add(m.x, m.y);
	if (minX > maxX) return;

	const float spanX = max(maxX - minX, 2000.f) * 1.15f;   // 여백 15%, 너무 가까이 붙지 않게 최소 20m
	const float spanY = max(maxY - minY, 2000.f) * 1.15f;
	_camX = (minX + maxX) * 0.5f;
	_camY = (minY + maxY) * 0.5f;
	_scale = min(_height / spanX, _width / spanY);
}

void Renderer::DrawGrid()
{
	// 격자 간격은 화면에서 40픽셀 이상이 되도록 1m·10m·100m 중에서 고른다.
	float step = 100.f;
	while (step * _scale < 40.f) step *= 10.f;

	float wx0, wy0, wx1, wy1;
	ScreenToWorld(0, 0, wx0, wy0);
	ScreenToWorld(static_cast<float>(_width), static_cast<float>(_height), wx1, wy1);

	glLineWidth(1.f);
	glBegin(GL_LINES);
	for (float y = floorf(wy0 / step) * step; y <= wy1; y += step)
	{
		const bool major = fmodf(fabsf(y), step * 10.f) < 1.f;
		glColor4f(1, 1, 1, major ? 0.12f : 0.05f);
		float sx, sy;
		WorldToScreen(0, y, sx, sy);
		glVertex2f(sx, 0); glVertex2f(sx, static_cast<float>(_height));
	}
	for (float x = floorf(wx0 / step) * step; x <= wx1; x += step)
	{
		const bool major = fmodf(fabsf(x), step * 10.f) < 1.f;
		glColor4f(1, 1, 1, major ? 0.12f : 0.05f);
		float sx, sy;
		WorldToScreen(x, 0, sx, sy);
		glVertex2f(0, sy); glVertex2f(static_cast<float>(_width), sy);
	}
	glEnd();

	glColor4f(0.7f, 0.7f, 0.7f, 1.f);
	PrintText(static_cast<float>(_width - 150), 10.f, "grid %.0f m", step / 100.f);
}

void Renderer::Render(const vector<BotView>& bots, const vector<MonsterView>& monsters, const HudStats& stats)
{
	glClearColor(0.08f, 0.09f, 0.11f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);

	DrawGrid();

	// 몬스터 스폰 위치: 주황 십자
	glLineWidth(2.f);
	glColor4f(1.f, 0.6f, 0.1f, 0.8f);
	glBegin(GL_LINES);
	for (const auto& s : GMonsterSpawns)
	{
		float sx, sy;
		WorldToScreen(s.first, s.second, sx, sy);
		glVertex2f(sx - 7, sy); glVertex2f(sx + 7, sy);
		glVertex2f(sx, sy - 7); glVertex2f(sx, sy + 7);
	}
	glEnd();

	// 몬스터: 빨간 사각형, 죽은 몬스터(SC_DEAD 후 제거 전)는 회색
	glBegin(GL_QUADS);
	for (const MonsterView& m : monsters)
	{
		float sx, sy;
		WorldToScreen(m.x, m.y, sx, sy);
		if (m.dead) glColor4f(0.5f, 0.5f, 0.5f, 0.9f);
		else glColor4f(0.95f, 0.2f, 0.2f, 1.f);
		glVertex2f(sx - 5, sy - 5); glVertex2f(sx + 5, sy - 5); glVertex2f(sx + 5, sy + 5); glVertex2f(sx - 5, sy + 5);
	}
	glEnd();

	// 봇: 상태별 색 점
	glPointSize(bots.size() > 2000 ? 3.f : 5.f);
	glBegin(GL_POINTS);
	for (const BotView& b : bots)
	{
		switch (b.state)
		{
		case BotState::InGame: glColor4f(0.3f, 0.9f, 0.4f, 0.9f); break;
		case BotState::Connecting:
		case BotState::LoggingIn: continue;   // 위치를 아직 모른다(로그인 전)
		default: continue;                    // Closed
		}
		float sx, sy;
		WorldToScreen(b.x, b.y, sx, sy);
		glVertex2f(sx, sy);
	}
	glEnd();

	DrawHud(bots, monsters, stats);
	SwapBuffers(_hdc);
}

void Renderer::DrawHud(const vector<BotView>& bots, const vector<MonsterView>& monsters, const HudStats& stats)
{
	int connecting = 0, loggingIn = 0, inGame = 0, closed = 0;
	for (const BotView& b : bots)
	{
		switch (b.state)
		{
		case BotState::Connecting: ++connecting; break;
		case BotState::LoggingIn: ++loggingIn; break;
		case BotState::InGame: ++inGame; break;
		case BotState::Closed: ++closed; break;
		default: break;
		}
	}
	int deadMonsters = 0;
	for (const MonsterView& m : monsters) if (m.dead) ++deadMonsters;

	// 반투명 패널
	const float panelW = 470.f, panelH = 195.f, top = static_cast<float>(_height) - 10.f;
	glColor4f(0, 0, 0, 0.6f);
	glBegin(GL_QUADS);
	glVertex2f(10, top - panelH); glVertex2f(10 + panelW, top - panelH); glVertex2f(10 + panelW, top); glVertex2f(10, top);
	glEnd();

	float y = top - 22.f;
	const float x = 20.f, line = 19.f;
	glColor4f(1.f, 0.85f, 0.3f, 1.f);
	PrintText(x, y, "JH_MMO DummyClient  %s:%d  profile=%s hero=%s%s", GConfig.host.c_str(), GConfig.port,
		GConfig.hunt ? "hunt" : (GConfig.move ? "move" : "idle"), GConfig.hero.c_str(), GConfig.chaos ? " chaos" : "");
	y -= line;
	glColor4f(0.9f, 0.9f, 0.9f, 1.f);
	PrintText(x, y, "t=%.0fs  bots %d target | connecting %d  login %d  in-game %d", BotEngine::ElapsedSec(), GConfig.bots, connecting, loggingIn, inGame);
	y -= line;
	PrintText(x, y, "connect fail %lld  closed %d  login fail %lld  protocol err %lld",
		GStat.connectFail.load(), closed, GStat.loginFail.load(), GStat.protocolError.load());
	y -= line;
	PrintText(x, y, "ping p50/p95/p99  %.1f / %.1f / %.1f ms", stats.pingP50Ms, stats.pingP95Ms, stats.pingP99Ms);
	y -= line;
	PrintText(x, y, "packets  sent %.0f/s  recv %.0f/s", stats.sentPerSec, stats.recvPerSec);
	y -= line;
	if (stats.serverFound) PrintText(x, y, "server  cpu %.0f%%  mem %.0f MB", stats.serverCpuPct, stats.serverMemMb);
	else PrintText(x, y, "server  (%s not found on this PC)", GConfig.serverProc.c_str());
	y -= line;
	PrintText(x, y, "monsters seen %zu (dead %d) | kills %lld  move corrected %lld", monsters.size(), deadMonsters,
		GStat.monsterDead.load(), GStat.moveCorrected.load());
	y -= line * 1.5f;

	glColor4f(0.3f, 0.9f, 0.4f, 1.f); PrintText(x, y, "o bot");
	glColor4f(0.95f, 0.2f, 0.2f, 1.f); PrintText(x + 70, y, "# monster");
	glColor4f(0.6f, 0.6f, 0.6f, 1.f); PrintText(x + 170, y, "# dead");
	glColor4f(1.f, 0.6f, 0.1f, 1.f); PrintText(x + 250, y, "+ spawn");
	y -= line;
	glColor4f(0.6f, 0.6f, 0.6f, 1.f);
	PrintText(x, y, "wheel zoom | drag pan | F fit%s | Esc quit", _autoFit ? " (auto)" : "");
}

void Renderer::PrintText(float x, float y, const char* fmt, ...)
{
	char text[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);

	glRasterPos2f(x, y);
	glPushAttrib(GL_LIST_BIT);
	glListBase(_fontBase - 32);
	glCallLists(static_cast<GLsizei>(strlen(text)), GL_UNSIGNED_BYTE, text);
	glPopAttrib();
}
