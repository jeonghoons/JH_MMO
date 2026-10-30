// 부하 테스트 시각화 클라이언트. LoadTest와 같은 BotEngine으로 봇을 돌리고, 봇·몬스터·통계를 탑다운 화면으로 보여준다(시연용).
// 사용법: DummyClient.exe --bots 100 --ramp-rate 20 [--profile hunt] [--hero warrior|archer] [--chaos] [--host 127.0.0.1] [--port 8888] ...
//         인자는 LoadTest와 같다. --duration·--out은 쓰지 않고 창을 닫을 때까지 돈다.
#include <algorithm>
#include <cstdio>
#include <vector>

#include "Renderer.h"
#include <windowsx.h>

using namespace std;

static Renderer GRenderer;
static vector<BotView> GBots;
static vector<MonsterView> GMonsters;
static bool GDragging = false;
static POINT GLastMouse{};

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_SIZE:
		GRenderer.Resize(LOWORD(lParam), HIWORD(lParam));
		return 0;
	case WM_MOUSEWHEEL:
	{
		POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		ScreenToClient(hwnd, &p);   // 휠 메시지 좌표는 화면 기준
		GRenderer.Zoom(p.x, p.y, GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.2f : 1.f / 1.2f);
		return 0;
	}
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
		GDragging = true;
		GLastMouse = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		SetCapture(hwnd);
		return 0;
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
		GDragging = false;
		ReleaseCapture();
		return 0;
	case WM_MOUSEMOVE:
		if (GDragging)
		{
			const POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			GRenderer.Pan(p.x - GLastMouse.x, p.y - GLastMouse.y);
			GLastMouse = p;
		}
		return 0;
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) PostMessageW(hwnd, WM_CLOSE, 0, 0);
		else if (wParam == 'F') GRenderer.FitToContent(GBots, GMonsters);
		return 0;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int main(int argc, char** argv)
{
	if (!BotEngine::ParseArgs(argc, argv)) return 1;
	GConfig.trackWorld = true;

	printf("[dummyclient] host=%s:%d bots=%d ramp=%d/s threads=%d profile=%s hero=%s chaos=%d\n",
		GConfig.host.c_str(), GConfig.port, GConfig.bots, GConfig.rampRate, GConfig.threads,
		GConfig.hunt ? "hunt" : (GConfig.move ? "move" : "idle"), GConfig.hero.c_str(), GConfig.chaos ? 1 : 0);

	HINSTANCE inst = GetModuleHandleW(nullptr);
	WNDCLASSW wc{};
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.lpszClassName = L"JH_MMO_DummyClient";
	RegisterClassW(&wc);

	RECT rc{ 0, 0, 1280, 800 };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	HWND hwnd = CreateWindowW(wc.lpszClassName, L"JH_MMO DummyClient (부하 테스트 시각화)", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);
	if (!hwnd || !GRenderer.Initialize(hwnd))
	{
		printf("[dummyclient] OpenGL window init failed\n");
		return 1;
	}
	ShowWindow(hwnd, SW_SHOW);
	GetClientRect(hwnd, &rc);
	GRenderer.Resize(rc.right, rc.bottom);

	BotEngine::Start();
	ServerProcSampler proc;
	proc.Open(GConfig.serverProc);

	HudStats stats;
	stats.serverFound = proc.IsOpen();
	long long lastStatUs = BotEngine::NowUs();
	long long lastSent = 0, lastRecv = 0;

	MSG msg{};
	bool running = true;
	while (running)
	{
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT) { running = false; break; }
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		if (!running) break;

		BotEngine::UpdateRamp();
		BotEngine::GetSnapshot(GBots, GMonsters);

		// 1초마다 통계 구간을 갱신한다.
		const long long now = BotEngine::NowUs();
		if (now - lastStatUs >= 1000 * 1000LL)
		{
			const double dt = (now - lastStatUs) / 1e6;
			vector<uint32_t> ping, login;
			BotEngine::TakeSamples(ping, login);
			sort(ping.begin(), ping.end());
			stats.pingP50Ms = BotEngine::Percentile(ping, 0.50) / 1000.0;
			stats.pingP95Ms = BotEngine::Percentile(ping, 0.95) / 1000.0;
			stats.pingP99Ms = BotEngine::Percentile(ping, 0.99) / 1000.0;
			const long long sent = GStat.pktSent.load(), recv = GStat.pktRecv.load();
			stats.sentPerSec = (sent - lastSent) / dt;
			stats.recvPerSec = (recv - lastRecv) / dt;
			lastSent = sent; lastRecv = recv;
			proc.Sample(stats.serverCpuPct, stats.serverMemMb);
			lastStatUs = now;

			if (GRenderer.IsAutoFit()) GRenderer.FitToContent(GBots, GMonsters);
		}

		GRenderer.Render(GBots, GMonsters, stats);
		Sleep(15);
	}

	BotEngine::Stop();
	GRenderer.Shutdown();
	printf("[dummyclient] loginOk=%lld loginFail=%lld connectFail=%lld unexpectedClose=%lld protocolError=%lld kills=%lld\n",
		GStat.loginOk.load(), GStat.loginFail.load(), GStat.connectFail.load(), GStat.unexpectedClose.load(),
		GStat.protocolError.load(), GStat.monsterDead.load());
	return 0;
}
