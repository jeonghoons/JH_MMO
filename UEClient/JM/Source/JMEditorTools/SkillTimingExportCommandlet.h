#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SkillTimingExportCommandlet.generated.h"

// 사용법: UnrealEditor-Cmd.exe <uproject경로> -run=SkillTimingExport [-Csv=<SkillTable.csv 경로>] [-NoReimport] -unattended -nopause
// (보통 Common/DataTable/ExportSkillTiming.bat으로 실행)
//
// SkillTable.csv의 Montage 열에 지정된 몽타주마다:
//   1) AnimNotify_SkillHit의 시각(ms, 몽타주 RateScale 반영)을 읽어 HitDelayMs 열을 갱신한다. 서버 판정과 클라 이펙트가 이 값을 쓴다.
//   2) 노티파이가 없거나 둘 이상이면, 슬롯이 UpperBody/DefaultSlot이 아니면 경고한다.
// 다른 열은 건드리지 않는다. 끝나면 DT_SkillTable(JM Data 설정)을 Reimport해 저장한다.
UCLASS()
class JMEDITORTOOLS_API USkillTimingExportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	USkillTimingExportCommandlet();

	virtual int32 Main(const FString& Params) override;
};
