#pragma once
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include "Recast.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"

static const int NAVMESHSET_MAGIC = 'M' << 24 | 'S' << 16 | 'E' << 8 | 'T'; //'MSET';
static const int NAVMESHSET_VERSION = 1;

// ==========================================
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
// ==========================================

struct RawMapData {
    std::vector<float> vertices;
    std::vector<int> indices;
    float bmin[3];
    float bmax[3];
};

class RecastNavMesh
{
public:
    RecastNavMesh();
    ~RecastNavMesh();
    
public:
    bool Build(const std::string& filePath);
    bool SaveNavMeshBinary(const std::string& path);

private:
    bool LoadBinFile(const std::string& path, RawMapData& outData);
    void SaveToObj(const std::string& fileName, const rcPolyMesh& pmesh);
    // 가장 넓은 연결 영역의 폴리곤만 flags 1로 두고 나머지는 0으로 끈다(서버 필터가 제외).
    static void PruneIslands(rcPolyMesh& pmesh, float cellSize);

private:
    dtNavMesh* m_navMesh = nullptr;
    dtNavMeshQuery* m_navQuery = nullptr;
    dtQueryFilter m_filter;
};

