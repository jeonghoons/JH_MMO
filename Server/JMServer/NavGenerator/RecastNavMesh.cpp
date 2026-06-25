#include "RecastNavMesh.h"

RecastNavMesh::RecastNavMesh()
{
	m_filter.setIncludeFlags(0xFFFF);
	m_filter.setExcludeFlags(0);
}

RecastNavMesh::~RecastNavMesh()
{
	if (m_navQuery) dtFreeNavMeshQuery(m_navQuery);
	if (m_navMesh) dtFreeNavMesh(m_navMesh);
}

bool RecastNavMesh::Build(const std::string& filePath)
{
    RawMapData mapData;
    std::cout << "[Step 0] 파일 로드 시작..." << std::endl;
    if (!LoadBinFile(filePath, mapData)) {
        std::cerr << "파일을 찾을 수 없습니다: " << filePath << std::endl;
        return false;
    }
    std::cout << "[Step 0] 파일 로드 완료! (정점: " << mapData.vertices.size() / 3 << "개)" << std::endl;

    // --- 파라미터 셋팅 (언리얼 기본 ThirdPersonCharacter 기준) ---
    rcConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.cs = 20.0f; // 격자 크기
    cfg.ch = 10.0f; // 높이 해상도

    cfg.walkableHeight = (int)ceilf(176.0f / cfg.ch); // 키 176cm
    cfg.walkableRadius = (int)floorf(34.0f / cfg.cs);  // 반지름 34cm
    cfg.walkableClimb = (int)floorf(50.0f / cfg.ch);  // 계단 오르기 45cm
    cfg.walkableSlopeAngle = 50.0f;                   // 등반 각도 45도

    cfg.maxEdgeLen = (int)(1200.0f / cfg.cs);
    cfg.maxSimplificationError = 1.3f;
    cfg.minRegionArea = (int)rcSqr(8);
    cfg.mergeRegionArea = (int)rcSqr(20);
    cfg.maxVertsPerPoly = 6;
    cfg.detailSampleDist = 600.0f;
    cfg.detailSampleMaxError = 1.0f;

    rcVcopy(cfg.bmin, mapData.bmin);
    rcVcopy(cfg.bmax, mapData.bmax);
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

    std::cout << "========================================" << std::endl;
    std::cout << "[Debug] World Size: " << (mapData.bmax[0] - mapData.bmin[0]) << "cm x " << (mapData.bmax[2] - mapData.bmin[2]) << "cm" << std::endl;
    std::cout << "[Debug] Grid Size: " << cfg.width << " x " << cfg.height << std::endl;
    std::cout << "========================================" << std::endl;

    // --- Build Pipeline ---
    rcContext ctx;
    rcHeightfield* hf = rcAllocHeightfield();

    std::cout << "[Step 1] rcCreateHeightfield..." << std::endl;
    if (!rcCreateHeightfield(&ctx, *hf, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) return false;

    std::cout << "[Step 2] rcRasterizeTriangles..." << std::endl;
    std::vector<unsigned char> areas(mapData.indices.size() / 3, RC_WALKABLE_AREA);
    rcRasterizeTriangles(&ctx, mapData.vertices.data(), (int)mapData.vertices.size() / 3,
        mapData.indices.data(), areas.data(), (int)areas.size(), *hf, cfg.walkableClimb);

    std::cout << "[Step 3] rcFilterWalkable..." << std::endl;
    rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *hf);
    rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *hf);
    rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *hf);

    std::cout << "[Step 4] rcBuildCompactHeightfield..." << std::endl;
    rcCompactHeightfield* chf = rcAllocCompactHeightfield();
    rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *hf, *chf);

    std::cout << "[Step 5] rcErodeWalkableArea & DistanceField..." << std::endl;
    rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf);
    rcBuildDistanceField(&ctx, *chf);

    std::cout << "[Step 6] rcBuildRegions..." << std::endl;
    rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea);

    std::cout << "[Step 7] rcBuildContours..." << std::endl;
    rcContourSet* cset = rcAllocContourSet();
    rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset);

    std::cout << "[Step 8] rcBuildPolyMesh..." << std::endl;
    rcPolyMesh* pmesh = rcAllocPolyMesh();
    rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh);

    std::cout << "[Step 9] rcBuildPolyMeshDetail..." << std::endl;
    rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
    rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh);

    std::cout << "[Step 9] 완료! 생성된 폴리곤 수: " << pmesh->npolys << "개" << std::endl;

    if (pmesh->npolys == 0) {
        std::cerr << ">>> 에러: 살아남은 폴리곤이 0개입니다! <<<" << std::endl;
        return false;
    }

    for (int i = 0; i < pmesh->npolys; ++i) {
        pmesh->flags[i] = 1;
    }

    std::cout << "[Step 10] dtCreateNavMeshData..." << std::endl;
    dtNavMeshCreateParams params;
    memset(&params, 0, sizeof(params));
    params.verts = pmesh->verts;
    params.vertCount = pmesh->nverts;
    params.polys = pmesh->polys;
    params.polyAreas = pmesh->areas;
    params.polyFlags = pmesh->flags;
    params.polyCount = pmesh->npolys;
    params.nvp = pmesh->nvp;
    params.detailMeshes = dmesh->meshes;
    params.detailVerts = dmesh->verts;
    params.detailVertsCount = dmesh->nverts;
    params.detailTris = dmesh->tris;
    params.detailTriCount = dmesh->ntris;

    params.walkableHeight = 176.0f;
    params.walkableRadius = 34.0f;
    params.walkableClimb = 50.0f;
    rcVcopy(params.bmin, pmesh->bmin);
    rcVcopy(params.bmax, pmesh->bmax);
    params.cs = cfg.cs;
    params.ch = cfg.ch;
    params.buildBvTree = true;

    unsigned char* navData = 0;
    int navDataSize = 0;
    if (!dtCreateNavMeshData(&params, &navData, &navDataSize)) return false;

    m_navMesh = dtAllocNavMesh();
    m_navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
    m_navQuery = dtAllocNavMeshQuery();
    m_navQuery->init(m_navMesh, 2048);

    std::cout << "[Step 11] OBJ 검증용 데이터 저장..." << std::endl;
    SaveToObj("NavMesh_Output.obj", *pmesh);

    rcFreeHeightField(hf);
    rcFreeCompactHeightfield(chf);
    rcFreeContourSet(cset);
    rcFreePolyMesh(pmesh);
    rcFreePolyMeshDetail(dmesh);

    return true;
}

bool RecastNavMesh::SaveNavMeshBinary(const std::string& path)
{
    if (!m_navMesh) return false;

    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) {
        std::cerr << "파일을 생성할 수 없습니다: " << path << std::endl;
        return false;
    }

    NavMeshSetHeader header;
    header.magic = NAVMESHSET_MAGIC;
    header.version = NAVMESHSET_VERSION;
    header.numTiles = 0;

    const dtNavMesh* constNavMesh = m_navMesh;

    // 타일 개수 파악
    for (int i = 0; i < constNavMesh->getMaxTiles(); ++i) {
        const dtMeshTile* tile = constNavMesh->getTile(i);
        if (!tile || !tile->header || !tile->dataSize) continue;
        header.numTiles++;
    }

    // 헤더 저장
    memcpy(&header.params, m_navMesh->getParams(), sizeof(dtNavMeshParams));
    fwrite(&header, sizeof(NavMeshSetHeader), 1, fp);

    // 각 타일 데이터 저장
    for (int i = 0; i < constNavMesh->getMaxTiles(); ++i) {
        const dtMeshTile* tile = constNavMesh->getTile(i);
        if (!tile || !tile->header || !tile->dataSize) continue;

        NavMeshTileHeader tileHeader;
        tileHeader.tileRef = m_navMesh->getTileRef(tile);
        tileHeader.dataSize = tile->dataSize;

        fwrite(&tileHeader, sizeof(tileHeader), 1, fp);
        fwrite(tile->data, tile->dataSize, 1, fp);
    }

    fclose(fp);
    std::cout << "\n[완료] NavMesh 바이너리가 저장되었습니다: " << path << std::endl;
    return true;
}

bool RecastNavMesh::LoadBinFile(const std::string& path, RawMapData& outData)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    int32_t vCount;
    file.read((char*)&vCount, sizeof(int32_t));
    outData.vertices.resize(vCount * 3);

    outData.bmin[0] = outData.bmin[1] = outData.bmin[2] = FLT_MAX;
    outData.bmax[0] = outData.bmax[1] = outData.bmax[2] = -FLT_MAX;

    for (int i = 0; i < vCount; ++i) {
        float x, y, z;
        file.read((char*)&x, sizeof(float));
        file.read((char*)&y, sizeof(float));
        file.read((char*)&z, sizeof(float));

        outData.vertices[i * 3 + 0] = x;
        outData.vertices[i * 3 + 1] = y;
        outData.vertices[i * 3 + 2] = z;

        for (int j = 0; j < 3; ++j) {
            float v = outData.vertices[i * 3 + j];
            if (v < outData.bmin[j]) outData.bmin[j] = v;
            if (v > outData.bmax[j]) outData.bmax[j] = v;
        }
    }

    // outData.bmax[1] += 500.0f;

    int32_t iCount;
    file.read((char*)&iCount, sizeof(int32_t));
    outData.indices.resize(iCount);
    file.read((char*)outData.indices.data(), sizeof(int32_t) * iCount);
    return true;
}

void RecastNavMesh::SaveToObj(const std::string& fileName, const rcPolyMesh& pmesh)
{
    std::ofstream objFile(fileName);
    if (!objFile.is_open()) return;
    for (int i = 0; i < pmesh.nverts; ++i) {
        const unsigned short* v = &pmesh.verts[i * 3];
        objFile << "v " << pmesh.bmin[0] + v[0] * pmesh.cs << " "
            << pmesh.bmin[1] + v[1] * pmesh.ch << " "
            << pmesh.bmin[2] + v[2] * pmesh.cs << "\n";
    }
    for (int i = 0; i < pmesh.npolys; ++i) {
        const unsigned short* p = &pmesh.polys[i * pmesh.nvp * 2];
        objFile << "f ";
        for (int j = 0; j < pmesh.nvp; ++j) {
            if (p[j] == RC_MESH_NULL_IDX) break;
            objFile << (p[j] + 1) << " ";
        }
        objFile << "\n";
    }
}
