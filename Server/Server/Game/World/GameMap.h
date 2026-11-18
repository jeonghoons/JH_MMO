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
    string ZoneName;   // 채워져 있으면 SpawnTable의 (MapName, Zone) 행이 종류·수·리스폰을 정한다(MonsterTemplateId·MaxCount는 안 씀) (L3)
};

// 안전 구역(L3, D-195): 마커 위치가 중심인 원(수평 거리). 안의 플레이어는 몬스터의 대상이 되지 않고 피해를 주고받지 않으며, 몬스터는 들어오지 않는다.
struct SafeZonePoint
{
    int32_t PointId = 0;
    MapPosition Pos;
    float Radius = 0.f;
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
    const vector<SafeZonePoint>& GetSafeZones() const { return _safeZones; }
    // (x, y)가 안전 구역 안인가(수평 거리). 구역이 없는 맵은 항상 false
    bool IsInSafeZone(float x, float y) const
    {
        for (const SafeZonePoint& zone : _safeZones)
        {
            const float dx = x - zone.Pos.X, dy = y - zone.Pos.Y;
            if (dx * dx + dy * dy <= zone.Radius * zone.Radius) return true;
        }
        return false;
    }
    bool IsInSafeZone(const Protocol::PositionInfo& pos) const { return IsInSafeZone(pos.x(), pos.y()); }
    bool CanMove(const Protocol::PositionInfo& from, const Protocol::PositionInfo& to, float edgeToleranceCm = 100.f) const;
    bool IsOutOfBounds(const Protocol::PositionInfo& pos) const;

    NavmeshManager* GetNavManager() { return _navManager.get(); }
    float GetViewRadius() const { return _viewRadius; }
    float GetViewHysteresis() const { return _viewHysteresis; }
    // pos에서 반경 radius 안의 갈 수 있는 임의 지점(몬스터 배회 — 안전 구역 안은 고르지 않는다). 못 찾으면 pos
    Protocol::PositionInfo GetRandomPosNear(const Protocol::PositionInfo& pos, float radius) const;
    const AoiIndex& GetAoi() const { return _aoi; }
    // 바닥 드랍 전용 색인(A3.4). 캐릭터 시야(_aoi·_viewList)와 분리해 몬스터 이동·시야 감사·시야 지표에 영향을 주지 않는다.
    // 넣고 빼기와 플레이어별 드랍 시야는 Room이 맡는다(Room::CreateDrop/RemoveDrop/UpdateDropView).
    AoiIndex& GetDropAoi() { return _dropAoi; }
    const AoiIndex& GetDropAoi() const { return _dropAoi; }

private:
    weak_ptr<Room>             _ownerRoom;
    AoiIndex                   _aoi;
    AoiIndex                   _dropAoi;   // 바닥 드랍(OBJECT_TYPE_ITEM) 전용

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
    vector<SafeZonePoint> _safeZones;
    unique_ptr<NavmeshManager> _navManager;
};

