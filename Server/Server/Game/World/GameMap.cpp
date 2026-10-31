#include "pch.h"
#include "Game/World/GameMap.h"
#include <fstream>
#include <nlohmann/json.hpp>

CellPos GameMap::ToCellPos(const Protocol::PositionInfo& pos) const
{
    int x_index = static_cast<int>((pos.x() - _minX) / _cellSize);
    int y_index = static_cast<int>((pos.y() - _minY) / _cellSize);

    // 맵 밖 좌표가 들어와도 _grid 인덱스가 범위를 벗어나지 않게 가장자리 셀로 고정한다.
    x_index = std::clamp(x_index, 0, std::max(_gridWidth - 1, 0));
    y_index = std::clamp(y_index, 0, std::max(_gridHeight - 1, 0));

    return { x_index, y_index };
}

ViewUpdate GameMap::EnterMap(int objectId, const Protocol::PositionInfo& pos)
{
    CellPos cellPos = ToCellPos(pos);
    _grid[cellPos.y][cellPos.x].objectIds.push_back(objectId);
    _id2CellPos[objectId] = cellPos;

    // 처음 들어왔을 때는 주변 모든 객체가 '추가' 대상임
    ViewUpdate result;
    vector<CellPos> neighbors = GetNeighborCells(cellPos);
    for (const auto& pos : neighbors) {
        CollectObject(pos, result.entered);
    }
    return result;
}

ViewUpdate GameMap::UpdateMap(int objectId, const Protocol::PositionInfo& pos)
{
    CellPos newPos = ToCellPos(pos);
    CellPos oldPos = _id2CellPos[objectId];

    if (newPos == oldPos) return {}; // 같은 셀이면 시야 변화 없음

    auto& oldVec = _grid[oldPos.y][oldPos.x].objectIds;
    auto it = find(oldVec.begin(), oldVec.end(), objectId);
    if (it != oldVec.end()) {
        *it = oldVec.back();
        oldVec.pop_back();
    }
    _grid[newPos.y][newPos.x].objectIds.push_back(objectId);
    _id2CellPos[objectId] = newPos;

    ViewUpdate result;
    vector<CellPos> old_view = GetNeighborCells(oldPos);
    vector<CellPos> new_view = GetNeighborCells(newPos);

    for (const auto& cell : new_view) {
        if (find(old_view.begin(), old_view.end(), cell) == old_view.end()) {
            CollectObject(cell, result.entered);
        }
    }

    for (const auto& cell : old_view) {
        if (find(new_view.begin(), new_view.end(), cell) == new_view.end()) {
            CollectObject(cell, result.leaved);
        }
    }

    return result;
}

ViewUpdate GameMap::LeaveMap(int objectId)
{
    auto itLoc = _id2CellPos.find(objectId);
    if (itLoc == _id2CellPos.end()) return {};

    CellPos lastPos = itLoc->second;

    // 그리드에서 제거
    auto& vec = _grid[lastPos.y][lastPos.x].objectIds;
    auto it = find(vec.begin(), vec.end(), objectId);
    if (it != vec.end()) {
        *it = vec.back();
        vec.pop_back();
    }

    // 나갈 때는 시야에 있던 모든 것이 '삭제' 대상
    ViewUpdate result;
    vector<CellPos> surrounding = GetNeighborCells(lastPos);
    for (const auto& cp : surrounding) {
        CollectObject(cp, result.leaved);
    }

    _id2CellPos.erase(itLoc);
    return result;
}



void GameMap::CollectObject(CellPos pos, vector<int>& outList) const
{
    const vector<int>& ids = _grid[pos.y][pos.x].objectIds;
    outList.insert(outList.end(), ids.begin(), ids.end());
}

vector<CellPos> GameMap::GetNeighborCells(CellPos pos) const
{
    vector<CellPos> cells;
    int cellCount = _viewRangeCells * 2 + 1;
    cells.reserve(cellCount * cellCount);

    for (int y = pos.y - _viewRangeCells; y <= pos.y + _viewRangeCells; ++y) {
        for (int x = pos.x - _viewRangeCells; x <= pos.x + _viewRangeCells; ++x) {
            if (x >= 0 && x < _gridWidth && y >= 0 && y < _gridHeight) {
                cells.push_back({ x, y });
            }
        }
    }
    return cells;
}

bool GameMap::LoadMapData(const string& fileName)
{
    bool isSuccess = true;

    // 0. 시야(AOI) 수치
    if (const WorldData* world = DataManager::GetWorldData(fileName)) {
        _cellSize = static_cast<float>(world->cellSizeCm);
        _viewRangeCells = world->viewRangeCells;
        _viewRadius = static_cast<float>(world->viewRadiusCm);
        _viewHysteresis = static_cast<float>(world->viewHysteresisCm);
    }
    else {
        LOG_ERROR("GameMap", "WorldTable에 맵 '{}' 행이 없습니다", fileName);
        return false;
    }

    // 1. 지형 데이터 (NavMesh) 로드 및 Bounds 동적 추출
    _navManager = std::make_unique<NavmeshManager>();
    string navPath = "Resource/Map/" + fileName + "_Geo.nav";

    if (!_navManager->LoadNavMesh(navPath, _minX, _maxX, _minY, _maxY)) {
        LOG_ERROR("GameMap", "NavMesh 로드 실패: {}", navPath);
        isSuccess = false;
    }
    else {
        _minX -= _cellSize;
        _maxX += _cellSize;
        _minY -= _cellSize;
        _maxY += _cellSize;

        _gridWidth = static_cast<int>((_maxX - _minX) / _cellSize) + 1;
        _gridHeight = static_cast<int>((_maxY - _minY) / _cellSize) + 1;

        
        _grid.assign(_gridHeight, vector<Cell>(_gridWidth));

    }

    
    string logicPath = "../MapResource/Export/" + fileName + "/Logic/" + fileName + "_Logic.json";
    std::ifstream file(logicPath);
    if (!file.is_open())
    {
        LOG_ERROR("GameMap", "로직 데이터를 찾을 수 없습니다: {}", logicPath);
        isSuccess = false;
    }
    else
    {
        try
        {
            nlohmann::json root;
            file >> root;
            file.close();

            for (const auto& marker : root.at("markers"))
            {
                const string type = marker.at("type").get<string>();
                const auto& posJson = marker.at("position");

                MapPosition pos;
                pos.X = posJson.at("x").get<float>();
                pos.Y = posJson.at("y").get<float>();
                pos.Z = posJson.at("z").get<float>();
                pos.Yaw = posJson.value("yaw", 0.f);

                int32_t pointId = marker.value("pointId", 0);

                if (type == "PlayerStart")
                {
                    _playerStarts.push_back({ pointId, pos });
                }
                else if (type == "MonsterSpawn")
                {
                    MonsterSpawnPoint spawn;
                    spawn.PointId = pointId;
                    spawn.Pos = pos;
                    spawn.MonsterTemplateId = marker.value("monsterTemplateId", 0);
                    spawn.SpawnRadius = marker.value("spawnRadius", 0.f);
                    spawn.MaxCount = marker.value("maxCount", 1);
                    _monsterSpawns.push_back(spawn);
                }
                else if (type == "Npc")
                {
                    NpcSpawnPoint npc;
                    npc.PointId = pointId;
                    npc.Pos = pos;
                    npc.NpcTemplateId = marker.value("npcTemplateId", 0);
                    _npcSpawns.push_back(npc);
                }
                else if (type == "PortalTrigger")
                {
                    PortalTriggerPoint portal;
                    portal.PointId = pointId;
                    portal.Pos = pos;
                    portal.TriggerRadius = marker.value("triggerRadius", 0.f);
                    portal.TargetMap = marker.value("targetMap", string());
                    portal.TargetPointId = marker.value("targetPointId", 0);
                    _portalTriggers.push_back(portal);
                }
                else
                {
                    LOG_WARN("GameMap", "알 수 없는 마커 타입: {}", type);
                }
            }

            LOG_INFO("GameMap", "로직 데이터 로드 완료! (PlayerStart {}, MonsterSpawn {}, Npc {}, PortalTrigger {})",
                _playerStarts.size(), _monsterSpawns.size(), _npcSpawns.size(), _portalTriggers.size());
        }
        catch (const nlohmann::json::exception& e)
        {
            LOG_ERROR("GameMap", "로직 데이터(JSON) 파싱 실패: {} - {}", logicPath, e.what());
            isSuccess = false;
        }
    }

    return isSuccess;
}

bool GameMap::CanMove(const Protocol::PositionInfo& from, const Protocol::PositionInfo& to, float edgeToleranceCm) const
{
    return _navManager->CanMove(from, to, edgeToleranceCm);
}

bool GameMap::IsOutOfBounds(const Protocol::PositionInfo& pos) const
{
    return _navManager->IsOutOfBounds(pos);
}

Protocol::PositionInfo GameMap::GetRandomPosInCell(const Protocol::PositionInfo& pos) const
{
    CellPos cellPos = ToCellPos(pos);

    float cellMinX = _minX + (cellPos.x * _cellSize);
    float cellMinY = _minY + (cellPos.y * _cellSize);

    float margin = 50.0f;

    for (int i = 0; i < 10; ++i)
    {
        float randX = Utils::GetRandom<float>(cellMinX + margin, cellMinX + _cellSize - margin);
        float randY = Utils::GetRandom<float>(cellMinY + margin, cellMinY + _cellSize - margin);

        Protocol::PositionInfo randomDest;
        randomDest.set_x(randX);
        randomDest.set_y(randY);
        randomDest.set_z(pos.z());

        if (CanMove(pos, randomDest))
        {
            _navManager->AdjustPositionToNavMesh(randomDest);
            return randomDest;
        }
    }

    return pos;
}



