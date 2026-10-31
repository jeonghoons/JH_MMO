#include "pch.h"
#include "Game/World/GameMap.h"
#include <fstream>
#include <nlohmann/json.hpp>

ViewUpdate GameMap::EnterMap(int objectId, const Protocol::PositionInfo& pos)
{
    _aoi.Insert(objectId, pos.x(), pos.y());

    // 처음 들어왔을 때는 반경 R 안의 모든 객체가 '추가' 대상
    ViewUpdate result;
    _aoi.QueryCircle(pos.x(), pos.y(), _viewRadius, result.entered);
    std::erase(result.entered, objectId);
    return result;
}

ViewUpdate GameMap::UpdateMap(int objectId, const Protocol::PositionInfo& pos, const vector<int>& currentView)
{
    _aoi.Move(objectId, pos.x(), pos.y());

    ViewUpdate result;
    const unordered_set<int> inView(currentView.begin(), currentView.end());

    // 들어옴: 반경 R 안인데 아직 시야에 없는 것
    vector<int> nearby;
    _aoi.QueryCircle(pos.x(), pos.y(), _viewRadius, nearby);
    for (int id : nearby)
        if (id != objectId && !inView.count(id)) result.entered.push_back(id);

    // 나감: 시야에 있는데 R+H 밖으로 멀어진 것 (R ~ R+H 사이는 그대로 둔다 — 경계 깜빡임 방지)
    const float rOut = _viewRadius + _viewHysteresis;
    for (int id : currentView)
    {
        float x, y;
        if (!_aoi.TryGetPos(id, x, y)) continue;   // 이미 맵을 떠난 대상(LeaveMap에서 정리됨)
        const float dx = x - pos.x(), dy = y - pos.y();
        if (dx * dx + dy * dy > rOut * rOut) result.leaved.push_back(id);
    }
    return result;
}

ViewUpdate GameMap::LeaveMap(int objectId, const vector<int>& currentView)
{
    _aoi.Remove(objectId);

    // 나갈 때는 시야에 있던 모든 것이 '삭제' 대상 (시야는 대칭이라 나를 보던 쪽 = 내가 보던 쪽)
    ViewUpdate result;
    result.leaved = currentView;
    return result;
}

bool GameMap::LoadMapData(const string& fileName)
{
    bool isSuccess = true;

    // 0. 시야(AOI) 수치
    if (const WorldData* world = DataManager::GetWorldData(fileName)) {
        _cellSize = static_cast<float>(world->cellSizeCm);
        _quadSplitCount = world->quadSplitCount;
        _quadMinSize = static_cast<float>(world->quadMinSizeCm);
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

        _aoi.Init(_minX, _minY, _maxX, _maxY, _cellSize, _quadSplitCount, _quadMinSize);
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

Protocol::PositionInfo GameMap::GetRandomPosNear(const Protocol::PositionInfo& pos, float radius) const
{
    for (int i = 0; i < 10; ++i)
    {
        const float angle = Utils::GetRandom<float>(0.f, 6.2831853f);
        const float dist = radius * sqrtf(Utils::GetRandom<float>(0.f, 1.f));

        Protocol::PositionInfo randomDest;
        randomDest.set_x(pos.x() + cosf(angle) * dist);
        randomDest.set_y(pos.y() + sinf(angle) * dist);
        randomDest.set_z(pos.z());

        if (CanMove(pos, randomDest))
        {
            _navManager->AdjustPositionToNavMesh(randomDest);
            return randomDest;
        }
    }

    return pos;
}
