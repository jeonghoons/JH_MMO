#include "pch.h"
#include "Game/World/AoiIndex.h"

AoiIndex::AoiIndex() = default;
AoiIndex::~AoiIndex() = default;

void AoiIndex::Init(float minX, float minY, float maxX, float maxY, float cellSize, int splitCount, float minNodeSize)
{
	_minX = minX;
	_minY = minY;
	_cellSize = cellSize;
	_splitCount = splitCount;
	_minNodeSize = minNodeSize;
	_cols = std::max(1, static_cast<int>(std::ceil((maxX - minX) / cellSize)));
	_rows = std::max(1, static_cast<int>(std::ceil((maxY - minY) / cellSize)));

	_cells.clear();
	_locs.clear();
	_cells.reserve(static_cast<size_t>(_cols) * _rows);
	for (int r = 0; r < _rows; ++r)
	{
		for (int c = 0; c < _cols; ++c)
		{
			auto node = std::make_unique<Node>();
			node->minX = _minX + c * cellSize;
			node->minY = _minY + r * cellSize;
			node->maxX = node->minX + cellSize;
			node->maxY = node->minY + cellSize;
			_cells.push_back(std::move(node));
		}
	}
}

int AoiIndex::CellIndex(float x, float y) const
{
	const int c = std::clamp(static_cast<int>(std::floor((x - _minX) / _cellSize)), 0, _cols - 1);
	const int r = std::clamp(static_cast<int>(std::floor((y - _minY) / _cellSize)), 0, _rows - 1);
	return r * _cols + c;
}

// 0: 왼쪽 아래, 1: 오른쪽 아래, 2: 왼쪽 위, 3: 오른쪽 위 (노드 중심 기준)
int AoiIndex::Quadrant(const Node* node, float x, float y)
{
	const float midX = (node->minX + node->maxX) * 0.5f;
	const float midY = (node->minY + node->maxY) * 0.5f;
	return (x >= midX ? 1 : 0) + (y >= midY ? 2 : 0);
}

AoiIndex::Node* AoiIndex::FindLeaf(Node* root, float x, float y, std::vector<Node*>* path) const
{
	Node* node = root;
	while (true)
	{
		if (path) path->push_back(node);
		if (node->IsLeaf()) return node;
		node = node->child[Quadrant(node, x, y)].get();
	}
}

void AoiIndex::InsertInto(Node* root, const Entry& e)
{
	std::vector<Node*> path;
	Node* leaf = FindLeaf(root, e.x, e.y, &path);
	for (Node* n : path) ++n->count;
	leaf->items.push_back(e);

	if (static_cast<int>(leaf->items.size()) > _splitCount && (leaf->maxX - leaf->minX) >= _minNodeSize * 2.f)
		Split(leaf);
}

void AoiIndex::Split(Node* node)
{
	const float midX = (node->minX + node->maxX) * 0.5f;
	const float midY = (node->minY + node->maxY) * 0.5f;
	for (int q = 0; q < 4; ++q)
	{
		auto child = std::make_unique<Node>();
		child->minX = (q & 1) ? midX : node->minX;
		child->maxX = (q & 1) ? node->maxX : midX;
		child->minY = (q & 2) ? midY : node->minY;
		child->maxY = (q & 2) ? node->maxY : midY;
		child->depth = node->depth + 1;
		node->child[q] = std::move(child);
	}

	std::vector<Entry> items;
	items.swap(node->items);
	for (const Entry& e : items)
	{
		Node* child = node->child[Quadrant(node, e.x, e.y)].get();
		child->items.push_back(e);
		++child->count;
	}
	// 한 자식에 다 몰렸으면 그 자식도 이어서 나눈다(최소 크기에서 멈춘다).
	for (auto& child : node->child)
		if (static_cast<int>(child->items.size()) > _splitCount && (child->maxX - child->minX) >= _minNodeSize * 2.f)
			Split(child.get());
}

void AoiIndex::CollectItems(Node* node, std::vector<Entry>& out) const
{
	if (node->IsLeaf())
	{
		out.insert(out.end(), node->items.begin(), node->items.end());
		return;
	}
	for (auto& child : node->child) CollectItems(child.get(), out);
}

void AoiIndex::RemoveFrom(Node* root, int id, float x, float y)
{
	std::vector<Node*> path;
	Node* leaf = FindLeaf(root, x, y, &path);
	auto it = std::find_if(leaf->items.begin(), leaf->items.end(), [id](const Entry& e) { return e.id == id; });
	if (it == leaf->items.end()) return;
	*it = leaf->items.back();
	leaf->items.pop_back();
	for (Node* n : path) --n->count;

	// 위에서부터 보면서 인원이 충분히 줄어든 가장 높은 노드를 하나로 합친다.
	for (Node* n : path)
	{
		if (!n->IsLeaf() && n->count <= _splitCount / 2)
		{
			std::vector<Entry> items;
			CollectItems(n, items);
			for (auto& child : n->child) child.reset();
			n->items = std::move(items);
			break;
		}
	}
}

void AoiIndex::Insert(int id, float x, float y)
{
	const int cell = CellIndex(x, y);
	_locs[id] = { x, y, cell };
	InsertInto(_cells[cell].get(), { id, x, y });
}

void AoiIndex::Move(int id, float x, float y)
{
	auto it = _locs.find(id);
	if (it == _locs.end()) { Insert(id, x, y); return; }

	Loc& loc = it->second;
	const int cell = CellIndex(x, y);
	if (cell == loc.cell)
	{
		// 같은 잎 노드 안이면 좌표만 바꾼다.
		Node* root = _cells[cell].get();
		Node* oldLeaf = FindLeaf(root, loc.x, loc.y, nullptr);
		Node* newLeaf = FindLeaf(root, x, y, nullptr);
		if (oldLeaf == newLeaf)
		{
			for (Entry& e : oldLeaf->items)
				if (e.id == id) { e.x = x; e.y = y; break; }
			loc.x = x; loc.y = y;
			return;
		}
	}
	RemoveFrom(_cells[loc.cell].get(), id, loc.x, loc.y);
	loc = { x, y, cell };
	InsertInto(_cells[cell].get(), { id, x, y });
}

void AoiIndex::Remove(int id)
{
	auto it = _locs.find(id);
	if (it == _locs.end()) return;
	RemoveFrom(_cells[it->second.cell].get(), id, it->second.x, it->second.y);
	_locs.erase(it);
}

bool AoiIndex::TryGetPos(int id, float& x, float& y) const
{
	auto it = _locs.find(id);
	if (it == _locs.end()) return false;
	x = it->second.x;
	y = it->second.y;
	return true;
}

void AoiIndex::QueryNode(const Node* node, float x, float y, float r2, std::vector<int>& out) const
{
	if (node->count == 0) return;
	// 잎은 항목의 실제 거리로, 자식은 원과 자식 사각형의 최단 거리로 거른다.
	// (맵 범위 = 내비메시 + 셀 하나 여유. 그 밖 좌표는 가장자리 셀에 들어가지만 자식 가지치기에서 빠질 수 있다 — 서버 좌표는 내비메시 안이다)
	if (node->IsLeaf())
	{
		for (const Entry& e : node->items)
		{
			const float dx = e.x - x, dy = e.y - y;
			if (dx * dx + dy * dy <= r2) out.push_back(e.id);
		}
		return;
	}
	for (const auto& child : node->child)
	{
		const float dx = std::max({ child->minX - x, 0.f, x - child->maxX });
		const float dy = std::max({ child->minY - y, 0.f, y - child->maxY });
		if (dx * dx + dy * dy <= r2) QueryNode(child.get(), x, y, r2, out);
	}
}

void AoiIndex::QueryCircle(float x, float y, float r, std::vector<int>& out) const
{
	// 원의 AABB와 겹치는 셀만 본다. 가장자리 셀은 맵 밖 좌표를 담으므로 셀 범위는 클램프된 인덱스로 고른다.
	const int c0 = std::clamp(static_cast<int>(std::floor((x - r - _minX) / _cellSize)), 0, _cols - 1);
	const int c1 = std::clamp(static_cast<int>(std::floor((x + r - _minX) / _cellSize)), 0, _cols - 1);
	const int r0 = std::clamp(static_cast<int>(std::floor((y - r - _minY) / _cellSize)), 0, _rows - 1);
	const int r1 = std::clamp(static_cast<int>(std::floor((y + r - _minY) / _cellSize)), 0, _rows - 1);
	const float r2 = r * r;
	for (int row = r0; row <= r1; ++row)
		for (int col = c0; col <= c1; ++col)
			QueryNode(_cells[row * _cols + col].get(), x, y, r2, out);
}

void AoiIndex::CountNodes(const Node* node, int& nodes, int& depth) const
{
	++nodes;
	depth = std::max(depth, node->depth);
	if (!node->IsLeaf())
		for (const auto& child : node->child) CountNodes(child.get(), nodes, depth);
}

int AoiIndex::NodeCount() const
{
	int nodes = 0, depth = 0;
	for (const auto& cell : _cells) CountNodes(cell.get(), nodes, depth);
	return nodes;
}

int AoiIndex::MaxDepth() const
{
	int nodes = 0, depth = 0;
	for (const auto& cell : _cells) CountNodes(cell.get(), nodes, depth);
	return depth;
}
