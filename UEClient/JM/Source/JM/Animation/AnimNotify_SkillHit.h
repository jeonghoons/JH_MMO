#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_SkillHit.generated.h"

// 스킬 몽타주의 타격 프레임 표식. 런타임에는 아무 일도 하지 않는다.
// SkillTimingExport 커맨드렛이 이 노티파이의 시각을 읽어 SkillTable.csv의 HitDelayMs(서버 판정·클라 이펙트 시점)를 갱신한다.
// 판정의 권위는 서버의 CSV 값이며, 노티파이는 그 값을 뽑는 기준일 뿐이다. (Docs/DECISIONS.md 2026-10-27)
UCLASS(meta = (DisplayName = "Skill Hit"))
class JM_API UAnimNotify_SkillHit : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual FString GetNotifyName_Implementation() const override { return TEXT("SkillHit"); }
};
