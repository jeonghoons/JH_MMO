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

	bool LoadNavMesh(const std::string& path, float& outMinX, float& outMaxX, float& outMinY, float& outMaxY);

	bool IsOutOfBounds(const Protocol::PositionInfo& pos);

	bool CanMove(const Protocol::PositionInfo& startPos, const Protocol::PositionInfo& destPos);

	bool FindPath(const Protocol::PositionInfo& startPos, const Protocol::PositionInfo& destPos, std::vector<Protocol::PositionInfo>& outPath);
	
	bool RayCast(const Protocol::PositionInfo& startPos, const Protocol::PositionInfo& destPos);

	static float GetRandomFloat()
	{
		return (float)rand() / (float)RAND_MAX;
	}
	Protocol::PositionInfo GetRandomPosition();
private:
	// 언리얼(Z-up) <-> Detour(Y-up) 좌표 변환 유틸리티
	void UeToDetour(const Protocol::PositionInfo& uePos, float* detourPos);
	void DetourToUe(const float* detourPos, Protocol::PositionInfo& uePos);

private:
	dtNavMesh* _navMesh = nullptr;
	dtNavMeshQuery* _navQuery = nullptr;
	dtQueryFilter	_filter;
};

