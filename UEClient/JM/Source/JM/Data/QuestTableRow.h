#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "QuestTableRow.generated.h"

// 퀘스트 종류(QuestTable.Kind). CSV 값은 이름(Kill·Collect) 그대로다.
UENUM(BlueprintType)
enum class EQuestKind : uint8
{
	Kill,		// TargetId = 몬스터 종류 ID(MonsterTable). 진행 = 서버가 보낸 처치 수(QuestInfo.progress)
	Collect,	// TargetId = 아이템 ID(ItemTable). 진행 = 내 인벤토리 칸의 수량(서버 progress는 0) — 완료하면 서버가 가져간다
};

// Common/DataTable/QuestTable.csv 의 한 행. 컬럼명은 CSV 헤더와 1:1이다. RowName = 퀘스트 ID(1~) = QuestInfo.quest_id.
// 수락·진행·완료 판정과 보상 지급은 서버가 한다(Q1, D-261). 클라이언트는 창·HUD·NPC 표식에 쓸 글자와 목표 수만 읽는다.
USTRUCT(BlueprintType)
struct FQuestTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString DisplayName;
	// 이 퀘스트를 주고 받는 NPC(NpcTable ID = 그 NPC의 ObjectInfo.template_id)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 NpcId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EQuestKind Kind = EQuestKind::Kill;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 TargetId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Count = 0;
	// 안내용(서버는 수락을 막지 않는다)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 RecommendLevel = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int64 RewardExp = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int64 RewardGold = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 RewardItemId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 RewardItemCount = 0;
	// 퀘스트 창 설명(클라이언트 전용)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Description;
};
