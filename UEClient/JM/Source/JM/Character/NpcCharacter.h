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

private:
	// MonsterTable의 이 종류(template_id) 행대로 메시(스킨)와 크기 배율을 적용한다. 스폰 때 한 번 (MN1, D-198)
	void ApplyMonsterAppearance(int32 TemplateId);
	bool bAppearanceApplied = false;

public:
    

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
    class UWidgetComponent* HpBarComponent;

};
