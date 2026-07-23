#include "pch.h"
#include "GameMap.h"
#include <fstream>

CellPos GameMap::ToCellPos(const Protocol::PositionInfo& pos) const
{
    int x_index = static_cast<int>((pos.x() - _minX) / CELL_SIZE);
    int y_index = static_cast<int>((pos.y() - _minY) / CELL_SIZE);

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
    int cellCount = VIEW_RANGE_CELLS * 2 + 1;
    cells.reserve(cellCount * cellCount);

    for (int y = pos.y - VIEW_RANGE_CELLS; y <= pos.y + VIEW_RANGE_CELLS; ++y) {
        for (int x = pos.x - VIEW_RANGE_CELLS; x <= pos.x + VIEW_RANGE_CELLS; ++x) {
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

    // 1. 지형 데이터 (NavMesh) 로드 및 Bounds 동적 추출
    _navManager = std::make_unique<NavmeshManager>();
    string navPath = "Resource/Map/" + fileName + "_Geo.nav";

    if (!_navManager->LoadNavMesh(navPath, _minX, _maxX, _minY, _maxY)) {
        cout << "[GameMap] NavMesh 로드 실패: " << navPath << endl;
        isSuccess = false;
    }
    else {
        _minX -= CELL_SIZE;
        _maxX += CELL_SIZE;
        _minY -= CELL_SIZE;
        _maxY += CELL_SIZE;

        _gridWidth = static_cast<int>((_maxX - _minX) / CELL_SIZE) + 1;
        _gridHeight = static_cast<int>((_maxY - _minY) / CELL_SIZE) + 1;

        
        _grid.assign(_gridHeight, vector<Cell>(_gridWidth));

        /*cout << "[GameMap] NavMesh 기반 동적 Grid 생성 완료!" << endl;
        cout << " - Bounds X: " << _minX << " ~ " << _maxX << ", Y: " << _minY << " ~ " << _maxY << endl;
        cout << " - Grid Size: " << _gridWidth << " x " << _gridHeight << " cells" << endl;*/
    }

    
    string logicPath = "../MapResource/Export/" + fileName + "/Logic/" + fileName + "_Logic.bin";
    std::ifstream file(logicPath, std::ios::binary);
    if (!file.is_open())
    {
        cout << "[GameMap] 로직 데이터를 찾을 수 없습니다: " << logicPath << endl;
        isSuccess = false;
    }
    else
    {
        int32_t spawnCount = 0;
        file.read((char*)&spawnCount, sizeof(int32_t));

        if (spawnCount > 0)
        {
            _spawnPoints.resize(spawnCount);
            file.read((char*)_spawnPoints.data(), spawnCount * sizeof(ServerSpawnPoint));
        }

        file.close();
        cout << "[GameMap] 로직 데이터 로드 완료! " << endl;
    }

    return isSuccess;
}

bool GameMap::CanMove(const Protocol::PositionInfo& from, const Protocol::PositionInfo& to) const
{
    return _navManager->CanMove(from, to);
}

bool GameMap::IsOutOfBounds(const Protocol::PositionInfo& pos) const
{
    return _navManager->IsOutOfBounds(pos);
}

std::optional<ServerSpawnPoint> GameMap::GetSpawnPoint(int index) const
{
    if (index >= 0 && index < _spawnPoints.size())
    {
        return _spawnPoints[index];
    }
    return std::nullopt;
}

Protocol::PositionInfo GameMap::GetRandomPosInCell(const Protocol::PositionInfo& pos) const
{
    CellPos cellPos = ToCellPos(pos);

    float cellMinX = _minX + (cellPos.x * CELL_SIZE);
    float cellMinY = _minY + (cellPos.y * CELL_SIZE);

    float margin = 50.0f;

    for (int i = 0; i < 10; ++i)
    {
        float randX = Utils::GetRandom<float>(cellMinX + margin, cellMinX + CELL_SIZE - margin);
        float randY = Utils::GetRandom<float>(cellMinY + margin, cellMinY + CELL_SIZE - margin);

        Protocol::PositionInfo randomDest;
        randomDest.set_x(randX);
        randomDest.set_y(randY);
        randomDest.set_x(pos.z());

        if (CanMove(pos, randomDest))
        {
            return randomDest;
        }
    }

    return pos;
}



