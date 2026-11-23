#include "BotData.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

using namespace std;

namespace
{
	unordered_map<int, BotData::Skill> GSkills;
	int GAuctionNpc = 0;
	int GShopNpc = 0;
	int GDropOwnerSec = 30;
	bool GLoaded = false;

	// 쉼표로 나눈 칸(따옴표 없는 단순 CSV — DataTable 규약)
	vector<string> SplitCsv(const string& line)
	{
		vector<string> out;
		string cell;
		stringstream ss(line);
		while (getline(ss, cell, ',')) out.push_back(cell);
		if (!line.empty() && line.back() == ',') out.emplace_back();
		return out;
	}

	// 머리 줄의 열 이름 → 위치. 첫 열(Name)은 id
	bool ReadTable(const string& path, vector<string>& header, vector<vector<string>>& rows)
	{
		ifstream f(path);
		if (!f.is_open()) return false;
		string line;
		if (!getline(f, line)) return false;
		if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line = line.substr(3);   // UTF-8 BOM
		if (!line.empty() && line.back() == '\r') line.pop_back();
		header = SplitCsv(line);
		while (getline(f, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty()) continue;
			rows.push_back(SplitCsv(line));
		}
		return true;
	}

	int Col(const vector<string>& header, const char* name)
	{
		for (size_t i = 0; i < header.size(); ++i)
			if (header[i] == name) return static_cast<int>(i);
		return -1;
	}

	int IntAt(const vector<string>& row, int col)
	{
		return (col >= 0 && col < static_cast<int>(row.size())) ? atoi(row[col].c_str()) : 0;
	}
}

bool BotData::Load(const string& dir)
{
	vector<string> dirs;
	if (!dir.empty()) dirs.push_back(dir + "/");
	else dirs = { "../../Common/DataTable/", "../Common/DataTable/", "Common/DataTable/" };

	for (const string& d : dirs)
	{
		vector<string> header;
		vector<vector<string>> rows;
		if (!ReadTable(d + "SkillTable.csv", header, rows)) continue;
		const int cSlot = Col(header, "Slot"), cAnim = Col(header, "AnimMs"), cCool = Col(header, "CooldownMs"), cLock = Col(header, "MoveLockMs");
		if (cSlot < 0 || cAnim < 0 || cCool < 0 || cLock < 0) { printf("[bot] SkillTable.csv columns missing (%s)\n", d.c_str()); return false; }
		for (const vector<string>& r : rows)
		{
			Skill s;
			s.slot = IntAt(r, cSlot); s.animMs = IntAt(r, cAnim); s.cooldownMs = IntAt(r, cCool); s.moveLockMs = IntAt(r, cLock);
			GSkills[IntAt(r, 0)] = s;
		}

		header.clear(); rows.clear();
		if (ReadTable(d + "NpcTable.csv", header, rows))
		{
			const int cAuction = Col(header, "Auction"), cShop = Col(header, "ShopId");
			for (const vector<string>& r : rows)
			{
				if (GAuctionNpc == 0 && IntAt(r, cAuction) == 1) GAuctionNpc = IntAt(r, 0);
				if (GShopNpc == 0 && IntAt(r, cShop) > 0) GShopNpc = IntAt(r, 0);
			}
		}
		header.clear(); rows.clear();
		if (ReadTable(d + "EconomyTable.csv", header, rows) && !rows.empty())
		{
			const int cOwner = Col(header, "DropOwnerSec");
			if (cOwner >= 0) GDropOwnerSec = IntAt(rows[0], cOwner);
		}
		GLoaded = true;
		printf("[bot] data tables loaded: skills %zu, auction npc %d, shop npc %d (%s)\n", GSkills.size(), GAuctionNpc, GShopNpc, d.c_str());
		return true;
	}
	printf("[bot] data tables not found (use --data-dir) — attacks use a fixed 700ms interval and no move lock\n");
	return false;
}

bool BotData::Loaded() { return GLoaded; }

const BotData::Skill* BotData::FindSkill(int skillId)
{
	auto it = GSkills.find(skillId);
	return it == GSkills.end() ? nullptr : &it->second;
}

int BotData::AuctionNpcTemplate() { return GAuctionNpc; }
int BotData::ShopNpcTemplate() { return GShopNpc; }
int BotData::DropOwnerSec() { return GDropOwnerSec; }
