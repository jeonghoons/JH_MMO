#pragma once
#include "pch.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

struct NavMeshSetHeader {
    int magic;
    int version;
    int numTiles;
    dtNavMeshParams params;
};

struct NavMeshTileHeader {
    dtTileRef tileRef;
    int dataSize;
};

class NavmeshManager
{
public:
	NavmeshManager();
	~NavmeshManager();

	// 같은 path는 한 번만 읽어 Room 간에 공유한다. 쿼리 객체(_navQuery)는 스레드 안전하지 않으므로 인스턴스마다 만든다.
	bool LoadNavMesh(const std::string& path, float& outMinX, float& outMaxX, float& outMinY, float& outMaxY);

	bool IsOutOfBounds(const Protocol::PositionInfo& pos);

	// 시작점에서 목표까지 내비메시 위로 막힘 없이 갈 수 있는가. 목표가 내비메시 경계 밖 edgeToleranceCm(수평 거리) 이내면 허용한다.
	bool CanMove(const Protocol::PositionInfo& startPos, const Protocol::PositionInfo& destPos, float edgeToleranceCm = 100.f);

	bool FindPath(const Protocol::PositionInfo& startPos, const Protocol::PositionInfo& destPos, std::vector<Protocol::PositionInfo>& outPath);
	
	bool RayCast(const Protocol::PositionInfo& startPos, const Protocol::PositionInfo& destPos);

	static float GetRandomFloat()
	{
		return (float)rand() / (float)RAND_MAX;
	}
	Protocol::PositionInfo GetRandomPosition();
	bool AdjustPositionToNavMesh(Protocol::PositionInfo& pos);
private:
	static bool LoadNavMeshFile(const std::string& path, shared_ptr<dtNavMesh>& outMesh, float& outMinX, float& outMaxX, float& outMinY, float& outMaxY);

	// 언리얼(Z-up) <-> Detour(Y-up) 좌표 변환 유틸리티
	void UeToDetour(const Protocol::PositionInfo& uePos, float* detourPos);
	void DetourToUe(const float* detourPos, Protocol::PositionInfo& uePos);

private:
	shared_ptr<dtNavMesh> _navMesh;
	dtNavMeshQuery* _navQuery = nullptr;
	dtQueryFilter	_filter;
};

