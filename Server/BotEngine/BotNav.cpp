#include "BotNav.h"

#include <cstdio>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourAlloc.h"

using namespace std;

namespace
{
	// 서버 NavmeshManager::LoadNavMeshFile과 같은 파일 형식(NavGenerator 출력)
	constexpr int NAVMESHSET_MAGIC = 'M' << 24 | 'S' << 16 | 'E' << 8 | 'T';
	constexpr int NAVMESHSET_VERSION = 1;
	struct NavMeshSetHeader { int magic; int version; int numTiles; dtNavMeshParams params; };
	struct NavMeshTileHeader { dtTileRef tileRef; int dataSize; };

	string GDir;
	mutex GMeshMutex;
	unordered_map<string, shared_ptr<dtNavMesh>> GMeshes;   // 레벨 → 메시(없는 레벨은 nullptr로 기억)

	// 언리얼(Z-up) ↔ Detour(Y-up). 서버 NavmeshManager::UeToDetour/DetourToUe와 같다
	void UeToDetour(float x, float y, float z, float* d) { d[0] = y; d[1] = z; d[2] = -x; }
	void DetourToUe(const float* d, float& x, float& y, float& z) { x = -d[2]; y = d[0]; z = d[1]; }

	shared_ptr<dtNavMesh> LoadFile(const string& path)
	{
		FILE* fp = nullptr;
		if (fopen_s(&fp, path.c_str(), "rb") != 0 || fp == nullptr) return nullptr;

		NavMeshSetHeader header;
		if (fread(&header, sizeof(header), 1, fp) != 1 || header.magic != NAVMESHSET_MAGIC || header.version != NAVMESHSET_VERSION)
		{
			printf("[bot] nav: damaged file %s\n", path.c_str());
			fclose(fp);
			return nullptr;
		}
		dtNavMesh* mesh = dtAllocNavMesh();
		if (mesh == nullptr || dtStatusFailed(mesh->init(&header.params)))
		{
			if (mesh) dtFreeNavMesh(mesh);
			fclose(fp);
			return nullptr;
		}
		for (int i = 0; i < header.numTiles; ++i)
		{
			NavMeshTileHeader tileHeader;
			if (fread(&tileHeader, sizeof(tileHeader), 1, fp) != 1) break;
			if (!tileHeader.tileRef || !tileHeader.dataSize) break;
			unsigned char* data = static_cast<unsigned char*>(dtAlloc(tileHeader.dataSize, DT_ALLOC_PERM));
			if (data == nullptr) break;
			fread(data, tileHeader.dataSize, 1, fp);
			mesh->addTile(data, tileHeader.dataSize, DT_TILE_FREE_DATA, tileHeader.tileRef, 0);
		}
		fclose(fp);
		return shared_ptr<dtNavMesh>(mesh, [](dtNavMesh* m) { dtFreeNavMesh(m); });
	}

	shared_ptr<dtNavMesh> GetMesh(const string& level)
	{
		if (level.empty()) return nullptr;
		lock_guard<mutex> lock(GMeshMutex);
		auto it = GMeshes.find(level);
		if (it != GMeshes.end()) return it->second;

		const string file = level + "_Geo.nav";
		vector<string> candidates;
		if (!GDir.empty()) candidates.push_back(GDir + "/" + file);
		else candidates = { "Resource/Map/" + file, "../Server/Resource/Map/" + file, "../../Server/Resource/Map/" + file };
		shared_ptr<dtNavMesh> mesh;
		for (const string& path : candidates)
		{
			mesh = LoadFile(path);
			if (mesh) { printf("[bot] nav loaded: %s\n", path.c_str()); break; }
		}
		if (!mesh) printf("[bot] nav not found for level '%s' (use --nav-dir) — bots keep the server's z and walk straight\n", level.c_str());
		GMeshes.emplace(level, mesh);
		return mesh;
	}

	// 워커 스레드마다 메시별 질의 객체 하나
	dtNavMeshQuery* GetQuery(const string& level)
	{
		struct QueryHolder
		{
			unordered_map<dtNavMesh*, dtNavMeshQuery*> queries;
			vector<shared_ptr<dtNavMesh>> keep;   // 질의가 쓰는 동안 메시가 살아 있게
			~QueryHolder() { for (auto& [mesh, q] : queries) dtFreeNavMeshQuery(q); }
		};
		thread_local QueryHolder holder;

		shared_ptr<dtNavMesh> mesh = GetMesh(level);
		if (!mesh) return nullptr;
		auto it = holder.queries.find(mesh.get());
		if (it != holder.queries.end()) return it->second;
		dtNavMeshQuery* q = dtAllocNavMeshQuery();
		if (q == nullptr || dtStatusFailed(q->init(mesh.get(), 2048)))
		{
			if (q) dtFreeNavMeshQuery(q);
			return nullptr;
		}
		holder.keep.push_back(mesh);
		holder.queries.emplace(mesh.get(), q);
		return q;
	}

	const dtQueryFilter& Filter()
	{
		static const dtQueryFilter filter = [] { dtQueryFilter f; f.setIncludeFlags(0xFFFF); f.setExcludeFlags(0); return f; }();
		return filter;
	}
}

void BotNav::SetDir(const string& dir) { GDir = dir; }

bool BotNav::Has(const string& level) { return GetMesh(level) != nullptr; }

bool BotNav::SnapZ(const string& level, float x, float y, float zHint, float& outZ)
{
	dtNavMeshQuery* q = GetQuery(level);
	if (q == nullptr) return false;
	float pos[3];
	UeToDetour(x, y, zHint, pos);
	// 서버 AdjustPositionToNavMesh와 같은 범위(수평 50cm, 위아래 4m)
	const float extents[3] = { 50.f, 400.f, 50.f };
	dtPolyRef ref = 0;
	float nearest[3];
	if (dtStatusFailed(q->findNearestPoly(pos, extents, &Filter(), &ref, nearest)) || ref == 0) return false;
	// 그 폴리곤 위 (x, y) 지점의 높이. 메시 경계 밖이라 높이를 못 구하면 가장 가까운 점의 높이
	float h = nearest[1];
	if (dtStatusFailed(q->getPolyHeight(ref, pos, &h))) h = nearest[1];
	outZ = h;
	return true;
}

bool BotNav::FindPath(const string& level, float sx, float sy, float sz, float tx, float ty, vector<array<float, 3>>& out)
{
	out.clear();
	dtNavMeshQuery* q = GetQuery(level);
	if (q == nullptr) return false;

	float start[3], target[3];
	UeToDetour(sx, sy, sz, start);
	UeToDetour(tx, ty, sz, target);
	const float startExt[3] = { 100.f, 400.f, 100.f };
	const float targetExt[3] = { 300.f, 3000.f, 300.f };   // 목표의 높이는 모른다(스폰 마커·몬스터는 x, y만 앎)
	dtPolyRef startRef = 0, endRef = 0;
	float startPt[3], endPt[3];
	q->findNearestPoly(start, startExt, &Filter(), &startRef, startPt);
	if (startRef == 0) return false;
	q->findNearestPoly(target, targetExt, &Filter(), &endRef, endPt);
	if (endRef == 0) return false;

	dtPolyRef polys[256];
	int polyCount = 0;
	q->findPath(startRef, endRef, startPt, endPt, &Filter(), polys, &polyCount, 256);
	if (polyCount <= 0) return false;
	// 끝까지 못 가는 길(partial)이면 마지막 폴리곤 안에서 목표에 가장 가까운 점으로
	float goal[3] = { endPt[0], endPt[1], endPt[2] };
	if (polys[polyCount - 1] != endRef)
		q->closestPointOnPoly(polys[polyCount - 1], endPt, goal, nullptr);

	float straight[64 * 3];
	int straightCount = 0;
	q->findStraightPath(startPt, goal, polys, polyCount, straight, nullptr, nullptr, &straightCount, 64);
	for (int i = 1; i < straightCount; ++i)   // 0번은 시작점
	{
		array<float, 3> p;
		DetourToUe(&straight[i * 3], p[0], p[1], p[2]);
		out.push_back(p);
	}
	return !out.empty();
}
