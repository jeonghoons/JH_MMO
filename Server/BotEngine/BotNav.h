#pragma once
// 봇의 내비메시(A5.1, D-293): 서버와 같은 .nav 파일(Server/Server/Resource/Map/<레벨>_Geo.nav)을 읽어
// 이동 패킷의 높이(z)를 바닥에 맞추고, 목표까지 걷는 영역 위의 길을 찾는다.
// 실제 클라이언트는 UE 충돌로 바닥을 딛고 길을 돌아가므로, 봇도 같은 모양의 이동을 보내야 서버 부하(이동 검증·거부 보정)가 실제와 같다.
// 메시는 레벨마다 한 번 읽어 모든 워커가 읽기 전용으로 공유하고, 질의 객체(dtNavMeshQuery)는 스레드 안전하지 않아 워커 스레드마다 따로 만든다.
#include <array>
#include <string>
#include <vector>

namespace BotNav
{
	// 메시 파일을 찾을 폴더(--nav-dir). 비면 작업 폴더 기준 후보(Resource/Map/, ../Server/Resource/Map/, ../../Server/Resource/Map/)
	void SetDir(const std::string& dir);

	// 그 레벨의 메시가 있는가(처음 부를 때 읽음, 없으면 한 번만 경고). level이 비면 false
	bool Has(const std::string& level);

	// (x, y) 바로 아래·위의 걷는 바닥 높이(캐릭터 중심이 아님 — 부르는 쪽이 올림). zHint 위아래 4m 안에서 찾고, 못 찾으면 false(outZ는 그대로)
	bool SnapZ(const std::string& level, float x, float y, float zHint, float& outZ);

	// (sx, sy, sz)에서 (tx, ty)까지 걷는 길의 꺾이는 점(UE 좌표, 시작점 제외, 마지막 = 도착점).
	// 목표에 닿을 수 없으면 가장 가까이 갈 수 있는 곳까지의 길을 준다(partial). 시작점을 메시에서 못 찾으면 false
	bool FindPath(const std::string& level, float sx, float sy, float sz, float tx, float ty, std::vector<std::array<float, 3>>& out);
}
