#include "RecastNavMesh.h"
#include "DetourCommon.h"
#include <sstream>

// 검증용 질의 모드: NavGenerator.exe --query <nav 파일> <질의 파일>
// 서버 NavmeshManager와 같은 질의(탐색 범위·노드 2,048개·경로 폴리곤 256개·필터)를 .nav에 대고 실행해 결과를 CSV로 출력한다.
// 맵을 다시 내보낸 뒤 마커가 걷는 영역 위에 있는지, 먼 경로 탐색이 상한 안에서 끝나는지, 내비메시 높이가 지형과 맞는지 확인하는 데 쓴다.
// 좌표는 모두 UE 좌표(cm). Detour(x, y, z) = UE(y, z, -x).
//
// 질의 파일 한 줄 = 질의 하나 (공백 구분, #로 시작하면 주석)
//   near <id> <x> <y> <z> <수평 범위> <높이 범위>      가장 가까운 폴리곤 (서버 AdjustPositionToNavMesh = 50 400, CanMove 시작점 = 200 300)
//   path <id> <x1> <y1> <z1> <x2> <y2> <z2>            서버 FindPath와 같음 (범위 100 200)
//   route <id> <x1> <y1> <z1> <x2> <y2> <z2>           path와 같고, 꺾임점을 pt 줄로 덧붙임
//   move <id> <x1> <y1> <z1> <x2> <y2> <z2> <허용 cm>  서버 CanMove와 같음 (범위 200 300) — NavmeshManager::CanMove를 고치면 여기도 같이 고친다

namespace
{
	void UeToDetour(float x, float y, float z, float* d) { d[0] = y; d[1] = z; d[2] = -x; }

	dtNavMesh* LoadNav(const std::string& path)
	{
		FILE* fp = fopen(path.c_str(), "rb");
		if (!fp) return nullptr;

		NavMeshSetHeader header;
		if (fread(&header, sizeof(header), 1, fp) != 1 || header.magic != NAVMESHSET_MAGIC || header.version != NAVMESHSET_VERSION) { fclose(fp); return nullptr; }

		dtNavMesh* mesh = dtAllocNavMesh();
		if (!mesh || dtStatusFailed(mesh->init(&header.params))) { fclose(fp); return nullptr; }

		for (int i = 0; i < header.numTiles; ++i) {
			NavMeshTileHeader th;
			if (fread(&th, sizeof(th), 1, fp) != 1 || !th.tileRef || !th.dataSize) break;
			unsigned char* data = (unsigned char*)dtAlloc(th.dataSize, DT_ALLOC_PERM);
			if (!data || fread(data, th.dataSize, 1, fp) != 1) break;
			mesh->addTile(data, th.dataSize, DT_TILE_FREE_DATA, th.tileRef, 0);
		}
		fclose(fp);
		return mesh;
	}
}

int RunQueryMode(const std::string& navPath, const std::string& queryPath)
{
	dtNavMesh* mesh = LoadNav(navPath);
	if (!mesh) { std::cerr << "nav 파일을 읽지 못했습니다: " << navPath << std::endl; return 1; }

	dtNavMeshQuery* query = dtAllocNavMeshQuery();
	query->init(mesh, 2048);   // 서버 NavmeshManager::LoadNavMesh와 같은 노드 수

	dtQueryFilter filter;
	filter.setIncludeFlags(0xFFFF);
	filter.setExcludeFlags(0);

	std::ifstream in(queryPath);
	if (!in.is_open()) { std::cerr << "질의 파일을 열지 못했습니다: " << queryPath << std::endl; return 1; }

	std::cout << "kind,id,ok,a,b,c,d,e,f" << std::endl;
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		std::istringstream ss(line);
		std::string kind, id;
		ss >> kind >> id;

		if (kind == "near") {
			// 출력: ok, 가장 가까운 점 x, y, 폴리곤 높이 z, 수평 거리, 0, 0
			float x, y, z, eh, ev;
			ss >> x >> y >> z >> eh >> ev;
			float p[3]; UeToDetour(x, y, z, p);
			const float ext[3] = { eh, ev, eh };
			dtPolyRef ref = 0; float np[3] = { 0, 0, 0 };
			query->findNearestPoly(p, ext, &filter, &ref, np);
			float h = np[1];
			if (ref) query->getPolyHeight(ref, np, &h);
			const float dx = np[0] - p[0], dz = np[2] - p[2];
			std::cout << "near," << id << "," << (ref ? 1 : 0) << "," << -np[2] << "," << np[0] << "," << h << "," << (ref ? sqrtf(dx * dx + dz * dz) : -1.f) << ",0,0" << std::endl;
		}
		else if (kind == "path" || kind == "route") {
			// 출력: ok(끝까지 닿음), 폴리곤 수, 꺾임점 수, 수평 길이, 끝점과 목표의 수평 거리, 부분 결과 여부, 노드 부족 여부
			float x1, y1, z1, x2, y2, z2;
			ss >> x1 >> y1 >> z1 >> x2 >> y2 >> z2;
			float s[3], e[3]; UeToDetour(x1, y1, z1, s); UeToDetour(x2, y2, z2, e);
			const float ext[3] = { 100.0f, 200.0f, 100.0f };
			dtPolyRef sr = 0, er = 0; float ns[3], ne[3];
			query->findNearestPoly(s, ext, &filter, &sr, ns);
			query->findNearestPoly(e, ext, &filter, &er, ne);
			if (!sr || !er) { std::cout << "path," << id << ",0,0,0,0,-1," << (sr ? 0 : 1) << "," << (er ? 0 : 1) << std::endl; continue; }

			dtPolyRef polys[256]; int n = 0;
			const dtStatus st = query->findPath(sr, er, ns, ne, &filter, polys, &n, 256);
			float sp[256 * 3]; int sn = 0;
			if (n > 0) query->findStraightPath(ns, ne, polys, n, sp, nullptr, nullptr, &sn, 256);
			float len = 0;
			for (int i = 1; i < sn; ++i) {
				const float dx = sp[i * 3] - sp[(i - 1) * 3], dz = sp[i * 3 + 2] - sp[(i - 1) * 3 + 2];
				len += sqrtf(dx * dx + dz * dz);
			}
			float endDist = -1;
			if (sn > 0) { const float dx = sp[(sn - 1) * 3] - ne[0], dz = sp[(sn - 1) * 3 + 2] - ne[2]; endDist = sqrtf(dx * dx + dz * dz); }
			const bool partial = dtStatusDetail(st, DT_PARTIAL_RESULT);
			const bool nodes = dtStatusDetail(st, DT_OUT_OF_NODES);
			const bool reached = n > 0 && polys[n - 1] == er && !partial;
			std::cout << "path," << id << "," << (reached ? 1 : 0) << "," << n << "," << sn << "," << len << "," << endDist << "," << (partial ? 1 : 0) << "," << (nodes ? 1 : 0) << std::endl;
			// route: 꺾임점을 UE 좌표로 한 줄씩 덧붙인다(시험 클라이언트가 서버 경로대로 걷는 데 씀)
			if (kind == "route")
				for (int i = 0; i < sn; ++i)
					std::cout << "pt," << id << "," << i << "," << -sp[i * 3 + 2] << "," << sp[i * 3] << "," << sp[i * 3 + 1] << ",0,0,0" << std::endl;
		}
		else if (kind == "move") {
			// 출력: ok(서버 CanMove 결과), 막힌 비율 t, 막힌 점과 목표의 수평 거리, 돌아가는 길 규칙으로 통과했는지(1), 0, 0, 0
			float x1, y1, z1, x2, y2, z2, tol;
			ss >> x1 >> y1 >> z1 >> x2 >> y2 >> z2 >> tol;
			float slack = 2.0f * tol;   // 서버 값. 시험용으로 줄 끝에 다른 값을 줄 수 있다
			{ float v; if (ss >> v) slack = v; }
			float s[3], e[3]; UeToDetour(x1, y1, z1, s); UeToDetour(x2, y2, z2, e);
			const float ext[3] = { 200.0f, 300.0f, 200.0f };
			dtPolyRef sr = 0; float ns[3];
			query->findNearestPoly(s, ext, &filter, &sr, ns);
			if (!sr) { std::cout << "move," << id << ",0,-1,-1,0,0,0,0" << std::endl; continue; }

			float t = 0, hn[3]; dtPolyRef rp[256]; int rn = 0;
			const dtStatus st = query->raycast(sr, ns, e, &filter, &t, hn, rp, &rn, 256);
			bool ok = false; float hitDist = 0;
			if (dtStatusSucceed(st)) {
				if (t >= 1.0f) ok = true;
				else {
					float hp[3]; dtVlerp(hp, ns, e, t);
					const float dx = hp[0] - e[0], dz = hp[2] - e[2];
					hitDist = sqrtf(dx * dx + dz * dz);
					ok = hitDist < tol;
				}
			}
			// 서버 CanMove의 보완 규칙(D-186)과 같은 내용: 목표가 걷는 영역에서 허용치 안이고 돌아가는 길이 직선 이동 + 허용치 × 2 이하면 통과
			int detour = 0;
			if (!ok) {
				const float dext[3] = { tol, 300.0f, tol };
				dtPolyRef dr = 0; float nd[3];
				query->findNearestPoly(e, dext, &filter, &dr, nd);
				const float ddx = nd[0] - e[0], ddz = nd[2] - e[2];
				if (dr && ddx * ddx + ddz * ddz <= tol * tol) {
					dtPolyRef dp[32]; int dn = 0;
					const dtStatus ps = query->findPath(sr, dr, ns, nd, &filter, dp, &dn, 32);
					if (!dtStatusFailed(ps) && !dtStatusDetail(ps, DT_PARTIAL_RESULT) && dn > 0 && dp[dn - 1] == dr) {
						float cs[16 * 3]; int cn = 0;
						const dtStatus ss2 = query->findStraightPath(ns, nd, dp, dn, cs, nullptr, nullptr, &cn, 16);
						if (!dtStatusFailed(ss2) && !dtStatusDetail(ss2, DT_BUFFER_TOO_SMALL) && cn >= 2) {
							float len = 0;
							for (int i = 1; i < cn; ++i) { const float ax = cs[i * 3] - cs[(i - 1) * 3], az = cs[i * 3 + 2] - cs[(i - 1) * 3 + 2]; len += sqrtf(ax * ax + az * az); }
							const float ex = cs[(cn - 1) * 3] - nd[0], ez = cs[(cn - 1) * 3 + 2] - nd[2];
							const float mx = e[0] - s[0], mz = e[2] - s[2];
							if (ex * ex + ez * ez <= 1.0f && len <= sqrtf(mx * mx + mz * mz) + slack) { ok = true; detour = 1; }
						}
					}
				}
			}
			std::cout << "move," << id << "," << (ok ? 1 : 0) << "," << (t > 1.0f ? 1.0f : t) << "," << hitDist << "," << detour << ",0,0,0" << std::endl;
		}
	}

	dtFreeNavMeshQuery(query);
	dtFreeNavMesh(mesh);
	return 0;
}
