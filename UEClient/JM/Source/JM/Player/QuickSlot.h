#pragma once

#include "CoreMinimal.h"

// 액티브 칸(숫자키 1~5) 하나의 내용 — DECISIONS D-148(2026-11-16).
// 칸마다 빈 칸 / 스킬 / 아이템 중 하나를 담는다. 칸 N에 현재 무기의 SkillTable 슬롯 N 스킬이 있으면 스킬이 우선이고,
// 없으면 서버에 저장된 배치(SC_INVENTORY.quick_slots)의 아이템 종류가 보인다 (A3.6, D-154).
enum class EQuickSlotKind : uint8
{
	Empty,
	Skill,   // Value = SkillTable의 Slot(현재 무기 기준) — 시전은 CS_ATTACK.skill_slot으로 보낸다
	Item,    // Value = ItemTable ID — 사용은 CS_USE_ITEM(item_db_id 0, item_id)으로 보낸다
};

struct FQuickSlot
{
	EQuickSlotKind Kind = EQuickSlotKind::Empty;
	int32 Value = 0;

	static FQuickSlot MakeSkill(int32 SkillSlot) { return FQuickSlot{ EQuickSlotKind::Skill, SkillSlot }; }
	static FQuickSlot MakeItem(int32 ItemId) { return FQuickSlot{ EQuickSlotKind::Item, ItemId }; }
};
