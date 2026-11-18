#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_SwingEnd.generated.h"

// 스킬 몽타주에서 "휘두르기가 끝나는" 프레임 표식(시작 → 휘두르기(중간에 Skill Hit) → 여기 → 돌아오기 → 끝). 런타임에는 아무 일도 하지 않는다.
// SkillTimingExport 커맨드렛이 이 노티파이의 시각을 읽어 SkillTable.csv의 MoveLockMs(시전부터 이동을 막는 시간)를 갱신한다.
// 권위는 서버의 CSV 값이며(서버가 그 시간 동안 이동을 거부한다), 노티파이는 그 값을 뽑는 기준일 뿐이다. (D-199)
UCLASS(meta = (DisplayName = "Swing End"))
class JM_API UAnimNotify_SwingEnd : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual FString GetNotifyName_Implementation() const override { return TEXT("SwingEnd"); }
};
