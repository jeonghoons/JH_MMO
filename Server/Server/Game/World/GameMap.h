#pragma once
#include "Game/World/NavmeshManager.h"
#include "Game/World/AoiIndex.h"
// 시야(AOI) 수치(셀 크기, 쿼드트리 분할 기준, 시야 반경 R·히스테리시스 H)는 WorldTable.csv(맵별 행)에서 읽는다.

struct ViewUpdate {
    vector<int> entered;   // 새로 시야에 들어온 객체들
    vector<int> leaved; // 시야에서 사라진 객체들
};

struct MapPosition
{
    float X = 0.f;
    float Y = 0.f;
    float Z = 0.f;
    float Yaw = 0.f;
};

struct PlayerStartPoint
{
    int32_t PointId = 0;
    MapPosition Pos;
};

struct MonsterSpawnPoint
{
    int32_t PointId = 0;
    MapPosition Pos;
    int32_t MonsterTemplateId = 0;
    float SpawnRadius = 0.f;
    int32_t MaxCount = 1;
};

struct NpcSpawnPoint
{
    int32_t PointId = 0;
    MapPosition Pos;
    int32_t NpcTemplateId = 0;
};

struct PortalTriggerPoint
{
    int32_t PointId = 0;
    MapPosition Pos;
    float TriggerRadius = 0.f;
    string TargetMap;
    int32_t TargetPointId = 0;
};

class Room;

class GameMap
{
public:
    GameMap() = default;
    ~GameMap() = default;
    void Init(weak_ptr<Room> room) { _ownerRoom = room; }

public:
    // 시야는 거리로 정한다: 들어옴 = 반경 R 안인데 아직 시야에 없음, 나감 = 시야에 있는데 R+H 밖.
    // currentView = 주체(subject)의 지금 시야 목록(Character::_viewList). 결과는 Room::UpdateView가 양쪽 목록과 패킷에 반영한다.
    ViewUpdate EnterMap(int objectId, const Protocol::PositionInfo& pos);
    ViewUpdate UpdateMap(int objectId, const Protocol::PositionInfo& pos, const vector<int>& currentView);
    ViewUpdate LeaveMap(int objectId, const vector<int>& currentView);

    bool LoadMapData(const string& fileName);
    const vector<PlayerStartPoint>& GetPlayerStarts() const { return _playerStarts; }
    const vector<MonsterSpawnPoint>& GetMonsterSpawns() const { return _monsterSpawns; }
    const vector<NpcSpawnPoint>& GetNpcSpawns() const { return _npcSpawns; }
    const vector<PortalTriggerPoint>& GetPortalTriggers() const { return _portalTriggers; }
    bool CanMove(const Protocol::PositionInfo& from, const Protocol::PositionInfo& to, float edgeToleranceCm = 100.f) const;
    bool IsOutOfBounds(const Protocol::PositionInfo& pos) const;

    NavmeshManager* GetNavManager() { return _navManager.get(); }
    float GetViewRadius() const { return _viewRadius; }
    float GetViewHysteresis() const { return _viewHysteresis; }
    // pos에서 반경 radius 안의 갈 수 있는 임의 지점(몬스터 배회). 못 찾으면 pos
    Protocol::PositionInfo GetRandomPosNear(const Protocol::PositionInfo& pos, float radius) const;
    const AoiIndex& GetAoi() const { return _aoi; }

private:
    weak_ptr<Room>             _ownerRoom;
    AoiIndex                   _aoi;

    float _minX = 0.f;
    float _maxX = 0.f;
    float _minY = 0.f;
    float _maxY = 0.f;
    float _cellSize = 5000.f;   // WorldTable.CellSizeCm
    int _quadSplitCount = 8;    // WorldTable.QuadSplitCount
    float _quadMinSize = 500.f; // WorldTable.QuadMinSizeCm
    float _viewRadius = 5000.f;      // WorldTable.ViewRadiusCm
    float _viewHysteresis = 500.f;   // WorldTable.ViewHysteresisCm

    vector<PlayerStartPoint> _playerStarts;
    vector<MonsterSpawnPoint> _monsterSpawns;
    vector<NpcSpawnPoint> _npcSpawns;
    vector<PortalTriggerPoint> _portalTriggers;
    unique_ptr<NavmeshManager> _navManager;
};

