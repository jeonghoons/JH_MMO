#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkillSlotWidget.generated.h"

struct FSkillTableRow;

// 전투 HUD의 스킬 슬롯 하나(아이콘, 키 글자, 쿨타임 덮개, 남은 초). 쿨타임 길이는 서버가 SC_ATTACK.cooldown_ms로 알려 준다.
UCLASS()
class JM_API USkillSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	int32 GetSkillSlot() const { return SkillSlot; }

	// 이 슬롯의 스킬. 아이콘이 없거나 스킬이 없으면 아이콘을 숨기고 키 글자만 보인다.
	void SetSkill(const FSkillTableRow* Skill);

	void StartCooldown(int32 CooldownMs);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 1~5 = 숫자키 (SkillTable의 Slot). WBP_CombatHud에 배치한 인스턴스마다 지정한다.
	// (UWidget::Slot과 겹치지 않도록 SkillSlot이라는 이름을 쓴다)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill Slot")
	int32 SkillSlot = 0;

	// 슬롯에 표시할 입력 키 이름(예: LMB, 1).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill Slot")
	FText KeyLabel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UImage> ImgIcon;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtKey;

	// 남은 쿨타임 비율만큼 채워지는 덮개. WBP에서 Fill Type = Bottom to Top, 반투명 검정으로 둔다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UProgressBar> BarCooldown;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtCooldown;

private:
	void ShowCooldown(bool bVisible);

	double CooldownEndTime = 0.0;   // FPlatformTime::Seconds 기준
	double CooldownDuration = 0.0;
};
