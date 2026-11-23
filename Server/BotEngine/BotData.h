#pragma once
// 봇이 읽는 데이터 테이블(A5.2, D-299): 서버와 같은 Common/DataTable CSV에서 봇 행동에 필요한 값만 읽는다.
// - SkillTable: 스킬 id → 슬롯·애니메이션 시간·쿨타임·이동 잠금 시간. 봇은 내 SC_ATTACK(받아들여진 시전)의 skill_id로 찾아
//   서버와 같은 규칙(기본 공격 = AttackSpeed로 나눔, Character::BeginCast)으로 다음 공격 시각과 이동 잠금을 정한다.
// - NpcTable: 경매 관리인(Auction=1) NPC 종류 id. 맵 로직 JSON의 Npc 마커와 SC_ADD_OBJECT.template_id를 이 값으로 찾는다.
// 수치를 봇 코드에 적지 않으려는 것(데이터 기반 규칙)이다. 파일을 못 찾으면 false — 부르는 쪽이 예전 동작(고정 간격)으로 돈다.
#include <string>

namespace BotData
{
	struct Skill
	{
		int slot = 0;
		int animMs = 0;
		int cooldownMs = 0;
		int moveLockMs = 0;
	};

	// dir이 비면 작업 폴더 기준 후보(../../Common/DataTable/, ../Common/DataTable/, Common/DataTable/)에서 찾는다
	bool Load(const std::string& dir);
	bool Loaded();
	// 없는 id면 nullptr
	const Skill* FindSkill(int skillId);
	// 경매 관리인 NPC 종류 id(NpcTable.Auction=1). 없으면 0
	int AuctionNpcTemplate();
	// 상점 NPC 종류 id(NpcTable.ShopId>0인 첫 행). 없으면 0
	int ShopNpcTemplate();
	// EconomyTable.DropOwnerSec(드랍 소유 시간, 초). 못 읽으면 30
	int DropOwnerSec();
}
