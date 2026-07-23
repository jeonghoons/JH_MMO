#pragma once
#include "NavmeshManager.h"
constexpr int CELL_SIZE = 1000;
const int VIEW_RANGE_CELLS = 1;

struct ViewUpdate {
    vector<int> entered;   // 새로 시야에 들어온 객체들
    vector<int> leaved; // 시야에서 사라진 객체들
};

struct Cell
{
    vector<int> objectIds;
};

struct CellPos {
    int x, y;

    bool operator==(const CellPos& other) const { return x == other.x && y == other.y; }
    bool operator!=(const CellPos& other) const { return !(*this == other); }
};
struct ServerSpawnPoint
{
    int32_t PointID;
    float X;
    float Y;
    float Z;
    float Yaw;
};
class Room;

class GameMap
{
public:
    GameMap() = default;
    ~GameMap() = default;
    void Init(weak_ptr<Room> room) { _ownerRoom = room; }

public:    
    ViewUpdate EnterMap(int objectId, const Protocol::PositionInfo& pos);
    ViewUpdate UpdateMap(int objectId, const Protocol::PositionInfo& pos);
    ViewUpdate LeaveMap(int objectId);

    bool LoadMapData(const string& fileName);
    const vector<ServerSpawnPoint>& GetSpawnPoints() const { return _spawnPoints; }
    std::optional<ServerSpawnPoint> GetSpawnPoint(int index) const;
    bool CanMove(const Protocol::PositionInfo& from, const Protocol::PositionInfo& to) const;
    bool IsOutOfBounds(const Protocol::PositionInfo& pos) const;

    NavmeshManager* GetNavManager() { return _navManager.get(); }
    Protocol::PositionInfo GetRandomPosInCell(const Protocol::PositionInfo& pos) const;
private:
    CellPos ToCellPos(const Protocol::PositionInfo& pos) const;
    void CollectObject(CellPos pos, vector<int>& outList) const;
    vector<CellPos> GetNeighborCells(CellPos pos) const;

private:
    weak_ptr<Room>             _ownerRoom;    
    vector<vector<Cell>>        _grid;
    unordered_map<int, CellPos>  _id2CellPos;

    float _minX = 0.f;
    float _maxX = 0.f;
    float _minY = 0.f;
    float _maxY = 0.f;
    int _gridWidth = 0;
    int _gridHeight = 0;

    vector<ServerSpawnPoint> _spawnPoints;
    unique_ptr<NavmeshManager> _navManager;
};

