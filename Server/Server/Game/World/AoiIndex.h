#pragma once
#include <memory>
#include <unordered_map>
#include <vector>

// 시야(AOI) 공간 인덱스: 맵을 큰 셀로 나누고, 셀마다 쿼드트리를 둔다.
// 노드에 항목이 splitCount를 넘으면 4분할(한 변이 minNodeSize*2 이상일 때만), 줄어들어 splitCount/2 이하가 되면 다시 합친다.
// 사람이 몰린 곳(핫존)만 잘게 나뉘므로 빈 곳은 가볍고 붐비는 곳은 반경 질의가 정확해진다.
// Room 잡 안에서만 쓴다(스레드 안전하지 않음). 좌표는 UE 기준 수평(x, y), cm.
class AoiIndex
{
public:
	AoiIndex();
	~AoiIndex();

	// 맵 범위(minX~maxX, minY~maxY)를 cellSize 셀로 나눈다. 범위 밖 좌표는 가장자리 셀에 들어간다.
	void Init(float minX, float minY, float maxX, float maxY, float cellSize, int splitCount, float minNodeSize);

	void Insert(int id, float x, float y);
	void Move(int id, float x, float y);   // 없는 id면 Insert
	void Remove(int id);

	// (x, y)에서 수평 거리 r 이하인 id를 out에 붙인다(자기 자신도 포함될 수 있다).
	void QueryCircle(float x, float y, float r, std::vector<int>& out) const;
	bool TryGetPos(int id, float& x, float& y) const;

	// 시험·지표용
	int NodeCount() const;
	int MaxDepth() const;

private:
	struct Entry { int id; float x, y; };
	struct Node
	{
		float minX, minY, maxX, maxY;
		int depth = 0;
		int count = 0;                      // 이 노드 아래 전체 항목 수
		std::vector<Entry> items;           // 잎 노드만 항목을 가진다
		std::unique_ptr<Node> child[4];     // 잎이면 모두 null
		bool IsLeaf() const { return !child[0]; }
	};
	struct Loc { float x, y; int cell; };

	int CellIndex(float x, float y) const;
	static int Quadrant(const Node* node, float x, float y);
	Node* FindLeaf(Node* root, float x, float y, std::vector<Node*>* path) const;
	void InsertInto(Node* root, const Entry& e);
	void RemoveFrom(Node* root, int id, float x, float y);
	void Split(Node* node);
	void CollectItems(Node* node, std::vector<Entry>& out) const;
	void QueryNode(const Node* node, float x, float y, float r2, std::vector<int>& out) const;
	void CountNodes(const Node* node, int& nodes, int& depth) const;

	std::vector<std::unique_ptr<Node>> _cells;
	std::unordered_map<int, Loc> _locs;
	float _minX = 0, _minY = 0, _cellSize = 1;
	int _cols = 0, _rows = 0;
	int _splitCount = 8;
	float _minNodeSize = 500;
};
