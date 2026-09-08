#include "pch.h"
#include "NavmeshManager.h"
#include "DetourCommon.h"
#include <cstdlib>

static const int NAVMESHSET_MAGIC = 'M' << 24 | 'S' << 16 | 'E' << 8 | 'T';
static const int NAVMESHSET_VERSION = 1;

NavmeshManager::NavmeshManager()
{
	_filter.setIncludeFlags(0xFFFF);
	_filter.setExcludeFlags(0);
}

NavmeshManager::~NavmeshManager()
{
	if (_navQuery) dtFreeNavMeshQuery(_navQuery);
	if (_navMesh) dtFreeNavMesh(_navMesh);
}

bool NavmeshManager::LoadNavMesh(const std::string& path, float& outMinX, float& outMaxX, float& outMinY, float& outMaxY)
{
	FILE* fp = nullptr;

	errno_t err = fopen_s(&fp, path.c_str(), "rb");

	if (err != 0 || !fp) {
		std::cerr << "[Navmesh] 파일을 찾을 수 없습니다: " << path << std::endl;
		return false;
	}

	NavMeshSetHeader header;
	size_t readLen = fread(&header, sizeof(NavMeshSetHeader), 1, fp);
	if (readLen != 1 || header.magic != NAVMESHSET_MAGIC || header.version != NAVMESHSET_VERSION) {
		std::cerr << "[Navmesh] 손상된 파일입니다." << std::endl;
		fclose(fp);
		return false;
	}

	_navMesh = dtAllocNavMesh();
	if (!_navMesh || dtStatusFailed(_navMesh->init(&header.params))) {
		fclose(fp);
		return false;
	}

	float dMinX = FLT_MAX, dMinY = FLT_MAX, dMinZ = FLT_MAX;
	float dMaxX = -FLT_MAX, dMaxY = -FLT_MAX, dMaxZ = -FLT_MAX;

	for (int i = 0; i < header.numTiles; ++i) {
		NavMeshTileHeader tileHeader;
		fread(&tileHeader, sizeof(tileHeader), 1, fp);
		if (!tileHeader.tileRef || !tileHeader.dataSize) break;

		unsigned char* data = (unsigned char*)dtAlloc(tileHeader.dataSize, DT_ALLOC_PERM);
		if (!data) break;
		fread(data, tileHeader.dataSize, 1, fp);

		_navMesh->addTile(data, tileHeader.dataSize, DT_TILE_FREE_DATA, tileHeader.tileRef, 0);

		// 현재 타일의 Bounding Box 갱신
		const dtMeshTile* tile = _navMesh->getTileByRef(tileHeader.tileRef);
		if (tile && tile->header) {
			if (tile->header->bmin[0] < dMinX) dMinX = tile->header->bmin[0];
			if (tile->header->bmin[1] < dMinY) dMinY = tile->header->bmin[1];
			if (tile->header->bmin[2] < dMinZ) dMinZ = tile->header->bmin[2];

			if (tile->header->bmax[0] > dMaxX) dMaxX = tile->header->bmax[0];
			if (tile->header->bmax[1] > dMaxY) dMaxY = tile->header->bmax[1];
			if (tile->header->bmax[2] > dMaxZ) dMaxZ = tile->header->bmax[2];
		}
	}

	fclose(fp);	

	_navQuery = dtAllocNavMeshQuery();
	_navQuery->init(_navMesh, 2048);

	outMinY = dMinX;
	outMaxY = dMaxX;
	outMinX = -dMaxZ;
	outMaxX = -dMinZ;

	std::cout << "[Navmesh] 로드 성공. " << std::endl;
	return true;
}

void NavmeshManager::UeToDetour(const Protocol::PositionInfo& uePos, float* detourPos)
{
	// 기존 제공해주신 매핑 기준: X->Z, Y->X, Z->Y (Y-up 변환)
	detourPos[0] = uePos.y();
	detourPos[1] = uePos.z();
	detourPos[2] = -uePos.x();
}

void NavmeshManager::DetourToUe(const float* detourPos, Protocol::PositionInfo& uePos)
{
	uePos.set_x(-detourPos[2]);
	uePos.set_y(detourPos[0]);
	uePos.set_z(detourPos[1]);
}

bool NavmeshManager::IsOutOfBounds(const Protocol::PositionInfo& pos)
{
	if (!_navQuery) return true; 

	float detourPos[3];
	UeToDetour(pos, detourPos);

	const float extents[3] = { 200.0f, 400.0f, 200.0f };
	dtPolyRef nearestRef = 0;
	float nearestPt[3];

	_navQuery->findNearestPoly(detourPos, extents, &_filter, &nearestRef, nearestPt);

	return (nearestRef == 0); 
}

bool NavmeshManager::CanMove(const Protocol::PositionInfo& startPos, const Protocol::PositionInfo& destPos)
{
	if (!_navQuery) return false;

	float dStart[3], dDest[3];
	UeToDetour(startPos, dStart);
	UeToDetour(destPos, dDest);

	const float extents[3] = { 200.0f, 300.0f, 200.0f };
	dtPolyRef startRef = 0;
	float nearestStart[3];

	_navQuery->findNearestPoly(dStart, extents, &_filter, &startRef, nearestStart);
	if (!startRef) {
		// cout << "오브젝트 근처에 폴리곤이 없습니다. " << endl;
		return false;
	}

	float t = 0; // 충돌 발생 지점 비율 (0.0 ~ 1.0)
	float hitNormal[3];
	dtPolyRef path[256];
	int pathCount = 0;

	dtStatus status = _navQuery->raycast(startRef, nearestStart, dDest, &_filter, &t, hitNormal, path, &pathCount, 256);

	if (dtStatusSucceed(status)) {
		if (t >= 1.0f) return true;

		float hitPos[3];
		dtVlerp(hitPos, nearestStart, dDest, t);

		const float tolerance = 100.0f;

		if (dtVdistSqr(hitPos, dDest) < (tolerance * tolerance)) {
			return true;
		}
	}
	return false;
}

bool NavmeshManager::FindPath(const Protocol::PositionInfo& startPos, const Protocol::PositionInfo& destPos, std::vector<Protocol::PositionInfo>& outPath)
{
	if (!_navQuery) return false;

	float dStart[3], dDest[3];
	UeToDetour(startPos, dStart);
	UeToDetour(destPos, dDest);

	const float extents[3] = { 100.0f, 200.0f, 100.0f };
	dtPolyRef startRef, endRef;
	float nearestStart[3], nearestEnd[3];

	_navQuery->findNearestPoly(dStart, extents, &_filter, &startRef, nearestStart);
	_navQuery->findNearestPoly(dDest, extents, &_filter, &endRef, nearestEnd);

	if (!startRef || !endRef) return false;

	dtPolyRef pathPolys[256];
	int pathCount = 0;
	_navQuery->findPath(startRef, endRef, nearestStart, nearestEnd, &_filter, pathPolys, &pathCount, 256);

	if (pathCount > 0) {
		float straightPath[256 * 3];
		int straightPathCount = 0;

		_navQuery->findStraightPath(nearestStart, nearestEnd, pathPolys, pathCount,
			straightPath, nullptr, nullptr, &straightPathCount, 256);

		for (int i = 0; i < straightPathCount; ++i) {
			Protocol::PositionInfo point;
			DetourToUe(&straightPath[i * 3], point);
			outPath.push_back(point);
		}
		return true;
	}
	return false;
}

bool NavmeshManager::RayCast(const Protocol::PositionInfo& startPos, const Protocol::PositionInfo& destPos)
{
	if (!_navQuery) return false;

	float dStart[3], dDest[3];
	UeToDetour(startPos, dStart);
	UeToDetour(destPos, dDest);

	const float extents[3] = { 50.0f, 100.0f, 50.0f };
	dtPolyRef startRef = 0;
	float nearestStart[3];

	_navQuery->findNearestPoly(dStart, extents, &_filter, &startRef, nearestStart);

	if (!startRef) {
		return false;
	}

	float t = 0;
	float hitNormal[3];
	dtPolyRef path[256];
	int pathCount = 0;

	dtStatus status = _navQuery->raycast(startRef, nearestStart, dDest, &_filter, &t, hitNormal, path, &pathCount, 256);

	if (dtStatusSucceed(status)) {
		if (t >= 0.95f) {
			return true;
		}
	}

	// 벽에 막힘
	return false;
}

Protocol::PositionInfo NavmeshManager::GetRandomPosition()
{
	Protocol::PositionInfo outPos{};

	if (!_navQuery || !_navMesh) return outPos;

	dtPolyRef randomRef = 0;
	float randomPt[3] = { 0.0f, 0.0f, 0.0f };

	for (int i = 0; i < 10; ++i)
	{
		dtStatus status = _navQuery->findRandomPoint(&_filter, GetRandomFloat, &randomRef, randomPt);

		if (dtStatusSucceed(status) && randomRef != 0)
		{
			unsigned char areaID = 0;
			_navMesh->getPolyArea(randomRef, &areaID);

			DetourToUe(randomPt, outPos);
			return outPos;
		}
	}

	return outPos;
}

bool NavmeshManager::AdjustPositionToNavMesh(Protocol::PositionInfo& pos)
{
	if (!_navQuery) return false;

    float dPos[3];
    UeToDetour(pos, dPos);

    const float extents[3] = { 50.0f, 400.0f, 50.0f }; 
    dtPolyRef nearestRef = 0;
    float nearestPt[3];

    _navQuery->findNearestPoly(dPos, extents, &_filter, &nearestRef, nearestPt);

    if (nearestRef != 0)
    {
        DetourToUe(nearestPt, pos);
        return true;
    }
    return false;
}
