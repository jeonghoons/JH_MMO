#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkillSlotWidget.generated.h"

struct FSkillTableRow;
struct FItemTableRow;

// 전투 HUD의 액티브 칸 하나(아이콘, 키 글자, 쿨타임 덮개, 남은 초) — 스킬 또는 아이템을 담는다(D-148). 쿨타임 길이는 서버가 SC_ATTACK.cooldown_ms로 알려 준다.
UCLASS()
class JM_API USkillSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	int32 GetSkillSlot() const { return SkillSlot; }

	// 이 칸의 스킬. 아이콘이 없거나 스킬이 없으면(빈 칸) 아이콘을 숨기고 키 글자만 보인다.
	void SetSkill(const FSkillTableRow* Skill);
	// 이 칸의 아이템(ItemTable Icon). 없으면 빈 칸과 같다.
	void SetItem(const FItemTableRow* Item);

	void StartCooldown(int32 CooldownMs);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 칸 번호 1~5 = 숫자키. WBP_CombatHud에 배치한 인스턴스마다 지정한다(지금 규칙 배치에서는 SkillTable의 Slot과 같다).
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
	void ShowIcon(const TSoftObjectPtr<UTexture2D>& Icon);

	double CooldownEndTime = 0.0;   // FPlatformTime::Seconds 기준
	double CooldownDuration = 0.0;
};
