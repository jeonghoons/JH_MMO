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

	bool IsOutOfBounds(const PositionInfo& pos);

	bool CanMove(const PositionInfo& startPos, const PositionInfo& destPos);

	bool FindPath(const PositionInfo& startPos, const PositionInfo& destPos, std::vector<PositionInfo>& outPath);
	
	bool RayCast(const PositionInfo& startPos, const PositionInfo& destPos);

	static float GetRandomFloat()
	{
		return (float)rand() / (float)RAND_MAX;
	}
	PositionInfo GetRandomPosition();
private:
	// 언리얼(Z-up) <-> Detour(Y-up) 좌표 변환 유틸리티
	void UeToDetour(const PositionInfo& uePos, float* detourPos);
	void DetourToUe(const float* detourPos, PositionInfo& uePos);

private:
	dtNavMesh* _navMesh = nullptr;
	dtNavMeshQuery* _navQuery = nullptr;
	dtQueryFilter	_filter;
};

