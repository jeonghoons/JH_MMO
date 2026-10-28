#include "SkillTimingExportCommandlet.h"

#include "Animation/AnimMontage.h"
// JM 모듈은 Public 폴더 없이 모듈 루트에 헤더를 둔다. include 경로를 추가하면 UHT가 JMEditorTools 경로를 잘못 잘라내므로 상대 경로로 쓴다.
#include "../JM/Animation/AnimNotify_SkillHit.h"
#include "../JM/Data/JMDataSettings.h"
#include "Engine/DataTable.h"
#include "EditorReimportHandler.h"
#include "FileHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	// 몽타주 재생 범위를 고르는 슬롯. (Docs/DECISIONS.md 2026-10-27: UpperBody = 상체, DefaultSlot = 전신)
	const TArray<FName> AllowedSlots = { FName(TEXT("UpperBody")), FName(TEXT("DefaultSlot")) };

	// 이 CSV는 따옴표를 쓰지 않는다(DESIGN_DATA.md). 따옴표가 있으면 열을 잘못 셀 수 있으므로 중단한다.
	bool SplitCsvLine(const FString& Line, TArray<FString>& OutCells)
	{
		if (Line.Contains(TEXT("\""))) return false;
		Line.ParseIntoArray(OutCells, TEXT(","), /*InCullEmpty*/ false);
		return true;
	}
}

USkillTimingExportCommandlet::USkillTimingExportCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 USkillTimingExportCommandlet::Main(const FString& Params)
{
	FString CsvPath;
	if (!FParse::Value(*Params, TEXT("Csv="), CsvPath))
	{
		CsvPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("../../Common/DataTable/SkillTable.csv"));
	}
	CsvPath = FPaths::ConvertRelativePathToFull(CsvPath);

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *CsvPath))
	{
		UE_LOG(LogTemp, Error, TEXT("[SkillTiming] CSV를 열 수 없습니다: %s"), *CsvPath);
		return 1;
	}

	const FString LineEnd = Text.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
	const bool bEndsWithNewline = Text.EndsWith(TEXT("\n"));
	TArray<FString> Lines;
	Text.ParseIntoArray(Lines, *LineEnd, /*InCullEmpty*/ false);
	if (bEndsWithNewline && Lines.Num() > 0 && Lines.Last().IsEmpty()) Lines.Pop();
	if (Lines.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[SkillTiming] CSV가 비어 있습니다: %s"), *CsvPath);
		return 1;
	}

	TArray<FString> Header;
	SplitCsvLine(Lines[0], Header);
	const int32 NameCol = Header.IndexOfByKey(TEXT("Name"));
	const int32 HitCol = Header.IndexOfByKey(TEXT("HitDelayMs"));
	const int32 MontageCol = Header.IndexOfByKey(TEXT("Montage"));
	const int32 AnimCol = Header.IndexOfByKey(TEXT("AnimMs"));
	if (NameCol == INDEX_NONE || HitCol == INDEX_NONE || MontageCol == INDEX_NONE || AnimCol == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("[SkillTiming] Name/HitDelayMs/AnimMs/Montage 열이 필요합니다."));
		return 1;
	}

	int32 Updated = 0, Warnings = 0;
	for (int32 i = 1; i < Lines.Num(); ++i)
	{
		if (Lines[i].TrimStartAndEnd().IsEmpty()) continue;

		TArray<FString> Cells;
		if (!SplitCsvLine(Lines[i], Cells) || Cells.Num() != Header.Num())
		{
			UE_LOG(LogTemp, Error, TEXT("[SkillTiming] %d번째 줄의 열 수가 헤더와 다르거나 따옴표가 있습니다."), i + 1);
			return 1;
		}

		const FString& SkillName = Cells[NameCol];
		const FString MontagePath = Cells[MontageCol].TrimStartAndEnd();
		if (MontagePath.IsEmpty()) continue;   // 몽타주를 지정하지 않은 행(몬스터 기본 공격 등)은 HitDelayMs·AnimMs를 직접 관리한다.

		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
		if (Montage == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SkillTiming] %s: 몽타주를 불러올 수 없습니다 (%s). HitDelayMs 유지"), *SkillName, *MontagePath);
			++Warnings;
			continue;
		}

		for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
		{
			if (!AllowedSlots.Contains(Track.SlotName))
			{
				UE_LOG(LogTemp, Warning, TEXT("[SkillTiming] %s: 슬롯 '%s'는 UpperBody/DefaultSlot이 아닙니다 (%s)"), *SkillName, *Track.SlotName.ToString(), *MontagePath);
				++Warnings;
			}
		}

		const float RateScale = Montage->RateScale > 0.f ? Montage->RateScale : 1.f;

		// 행동 시간: 몽타주 1회 길이
		const FString AnimValue = FString::FromInt(FMath::RoundToInt(Montage->GetPlayLength() / RateScale * 1000.f));
		if (Cells[AnimCol] != AnimValue)
		{
			UE_LOG(LogTemp, Display, TEXT("[SkillTiming] %s: AnimMs %s -> %s"), *SkillName, *Cells[AnimCol], *AnimValue);
			Cells[AnimCol] = AnimValue;
			Lines[i] = FString::Join(Cells, TEXT(","));
			++Updated;
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("[SkillTiming] %s: AnimMs %s (변경 없음)"), *SkillName, *AnimValue);
		}

		TArray<float> HitTimes;
		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			if (Cast<UAnimNotify_SkillHit>(Event.Notify))
			{
				HitTimes.Add(Event.GetTriggerTime());
			}
		}
		if (HitTimes.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SkillTiming] %s: Skill Hit 노티파이가 없습니다 (%s). HitDelayMs 유지"), *SkillName, *MontagePath);
			++Warnings;
			continue;
		}
		HitTimes.Sort();
		if (HitTimes.Num() > 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SkillTiming] %s: Skill Hit 노티파이가 %d개입니다. 첫 번째를 사용합니다."), *SkillName, HitTimes.Num());
			++Warnings;
		}

		const int32 HitMs = FMath::RoundToInt(HitTimes[0] / RateScale * 1000.f);
		const FString NewValue = FString::FromInt(HitMs);
		if (Cells[HitCol] != NewValue)
		{
			UE_LOG(LogTemp, Display, TEXT("[SkillTiming] %s: HitDelayMs %s -> %s"), *SkillName, *Cells[HitCol], *NewValue);
			Cells[HitCol] = NewValue;
			Lines[i] = FString::Join(Cells, TEXT(","));
			++Updated;
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("[SkillTiming] %s: HitDelayMs %s (변경 없음)"), *SkillName, *NewValue);
		}
	}

	if (Updated > 0)
	{
		FString Out = FString::Join(Lines, *LineEnd);
		if (bEndsWithNewline) Out += LineEnd;
		if (!FFileHelper::SaveStringToFile(Out, *CsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogTemp, Error, TEXT("[SkillTiming] CSV를 저장할 수 없습니다 (다른 프로그램이 열고 있는지 확인): %s"), *CsvPath);
			return 1;
		}
	}

	if (!FParse::Param(*Params, TEXT("NoReimport")))
	{
		UDataTable* Table = GetDefault<UJMDataSettings>()->SkillTable.LoadSynchronous();
		if (Table == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[SkillTiming] JM Data 설정의 SkillTable을 불러올 수 없습니다."));
			return 1;
		}
		if (!FReimportManager::Instance()->Reimport(Table, /*bAskForNewFileIfMissing*/ false, /*bShowNotification*/ false))
		{
			UE_LOG(LogTemp, Error, TEXT("[SkillTiming] %s Reimport 실패"), *Table->GetPathName());
			return 1;
		}
		if (!UEditorLoadingAndSavingUtils::SavePackages({ Table->GetPackage() }, /*bOnlyDirty*/ false))
		{
			UE_LOG(LogTemp, Error, TEXT("[SkillTiming] %s 저장 실패"), *Table->GetPathName());
			return 1;
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[SkillTiming] 완료: 갱신 %d행, 경고 %d건"), Updated, Warnings);
	return 0;
}
