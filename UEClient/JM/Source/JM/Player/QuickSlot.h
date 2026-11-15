#pragma once

#include "CoreMinimal.h"

// 액티브 칸(숫자키 1~5) 하나의 내용 — DECISIONS D-148(2026-11-16).
// 칸마다 빈 칸 / 스킬 / 아이템 중 하나를 담는다. 지금은 규칙 배치(칸 N = 현재 무기의 SkillTable 슬롯 N, 없으면 빈 칸)만 쓰고,
// 아이템 넣기와 배치 저장(서버)은 소비 아이템이 생길 때(A3.1 설계) 붙인다.
enum class EQuickSlotKind : uint8
{
	Empty,
	Skill,   // Value = SkillTable의 Slot(현재 무기 기준) — 시전은 CS_ATTACK.skill_slot으로 보낸다
	Item,    // Value = ItemTable ID (사용 패킷은 아직 없음)
};

struct FQuickSlot
{
	EQuickSlotKind Kind = EQuickSlotKind::Empty;
	int32 Value = 0;

	static FQuickSlot MakeSkill(int32 SkillSlot) { return FQuickSlot{ EQuickSlotKind::Skill, SkillSlot }; }
	static FQuickSlot MakeItem(int32 ItemId) { return FQuickSlot{ EQuickSlotKind::Item, ItemId }; }
};
