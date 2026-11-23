#pragma once

#include "CoreMinimal.h"
#include "Character/JMCharacterBase.h"
#include "NpcCharacter.generated.h"

UCLASS()
class JM_API ANpcCharacter : public AJMCharacterBase
{
	GENERATED_BODY()
public:
	ANpcCharacter();

	virtual void SetPlayerData(const Protocol::ObjectInfo& ObjInfo) override;

	virtual void OnDamaged(int damage, int remainHp) override;
	virtual void OnDead() override;
	// 서버가 주는 몬스터 z는 지면(내비메시) — 캡슐 중심까지 올린다(크기 배율 반영)
	virtual float GetServerZOffset() const override;
	virtual void SetServerHp(int32 Hp) override;
	virtual void Tick(float DeltaTime) override;

	// HP바 표시(FP1, D-286): 나 또는 파티원이 때렸을 때만 보이고, HP가 가득 차거나 대상이 우리 편에서 벗어나거나
	// 마지막으로 때린 뒤 HpBarShowSec가 지나고 우리 편을 노리지 않으면 숨는다.
	void OnHitByAlly();
	// 서버가 알려 준 지금 대상(ObjectInfo.target_id·SC_ATTACK.target_id)이 나·파티원인가
	void SetTargetIsAlly(bool bAlly);

	// 마지막으로 우리 편이 때린 뒤 HP바를 유지하는 시간(대상이 우리 편이면 계속 보임)
	UPROPERTY(EditAnywhere, Category = "UI")
	float HpBarShowSec = 10.f;

private:
	void SetHpBarShown(bool bShow);
	void ApplyHpBarVisibility();
	void RefreshHpBar();
	bool bHpBarShown = false;
	bool bTargetIsAlly = false;
	double LastAllyHitTime = -1000.0;

	// MonsterTable의 이 종류(template_id) 행대로 메시(스킨)와 크기 배율을 적용한다. 스폰 때 한 번 (MN1, D-198)
	void ApplyMonsterAppearance(int32 TemplateId);
	bool bAppearanceApplied = false;

public:
    

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
    class UWidgetComponent* HpBarComponent;

};
