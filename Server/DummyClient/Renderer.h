#pragma once
// 봇 엔진 스냅숏을 위에서 내려다본 2D 화면으로 그린다. 화면 위쪽 = 월드 +X, 오른쪽 = 월드 +Y (UE 탑뷰와 같은 방향).
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

#include "../BotEngine/BotEngine.h"

// 화면 왼쪽 위에 표시할 통계(1초 구간)
struct HudStats
{
	double pingP50Ms = 0, pingP95Ms = 0, pingP99Ms = 0;
	double sentPerSec = 0, recvPerSec = 0;
	double serverCpuPct = 0, serverMemMb = 0;
	bool serverFound = false;
};

class Renderer
{
public:
	bool Initialize(HWND hwnd);
	void Shutdown();
	void Resize(int width, int height);
	void Render(const std::vector<BotView>& bots, const std::vector<MonsterView>& monsters, const HudStats& stats);

	// 카메라: 휠 = 커서 기준 확대/축소, 드래그 = 이동, F = 전체 맞춤
	void Zoom(int screenX, int screenY, float factor);
	void Pan(int dxPixels, int dyPixels);
	void FitToContent(const std::vector<BotView>& bots, const std::vector<MonsterView>& monsters);
	bool IsAutoFit() const { return _autoFit; }

private:
	void WorldToScreen(float wx, float wy, float& sx, float& sy) const;
	void ScreenToWorld(float sx, float sy, float& wx, float& wy) const;
	void DrawGrid();
	void DrawHud(const std::vector<BotView>& bots, const std::vector<MonsterView>& monsters, const HudStats& stats);
	void PrintText(float x, float y, const char* fmt, ...);

	HWND _hwnd = nullptr;
	HDC _hdc = nullptr;
	HGLRC _hrc = nullptr;
	unsigned int _fontBase = 0;
	int _width = 1, _height = 1;

	float _camX = 0, _camY = 0;   // 화면 가운데의 월드 좌표
	float _scale = 0.05f;          // 월드 1cm당 픽셀
	bool _autoFit = true;          // 사용자가 카메라를 만지기 전까지는 봇이 늘어날 때마다 다시 맞춘다
};
