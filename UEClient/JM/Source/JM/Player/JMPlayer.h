#pragma once

#include "CoreMinimal.h"
#include "Character/JMCharacterBase.h"
#include "Data/ItemTableRow.h"
#include "JMPlayer.generated.h"

class UAnimMontage;
UCLASS()
class JM_API AJMPlayer : public AJMCharacterBase
{
	GENERATED_BODY()
public:
	AJMPlayer();
	virtual void SetPlayerData(const Protocol::ObjectInfo& ObjInfo) override;

protected:
	// 외형 = 체형 + 장착 목록(BodyTable·ItemTable, MC4.1)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment")
	TObjectPtr<class UEquipmentComponent> Equipment;

	void RefreshEquipment();

public:
	// 장착 무기의 타입(스킬 조회 키). 무기 슬롯이 비면 Unarmed. ObjectInfo.equip_items(서버 값)의 무기로 ItemTable을 조회한다.
	EWeaponType GetWeaponType() const;

	// SC_CHANGE_EQUIP 반영: 장착 목록과 서버가 계산한 최종 스탯을 그대로 넣고 외형을 바꾼다.
	void ApplyEquipChange(Protocol::EquipPart Part, int32 ItemId, const Protocol::StatInfo& Stat);

	// 무기 등에 메기(MC4.2d-2, DECISIONS 2026-11-09, 클라 연출 전용 — 모든 클라가 같은 이벤트로 각자 계산):
	// 공격(SC_ATTACK)·조준 시작 → 즉시 손, 조준 해제·공격 뒤(손에 든 동안 피격 포함) SheatheDelaySec이 지나면 넣기 동작 → SheatheSwapSec에 등.
	virtual int32 GetWeaponStance() const override;
	virtual void SetSheatheData(bool bInCanSheathe, float DelaySec, UAnimMontage* Montage, float SwapSec) override;
	virtual void SetSkillAiming(int32 SkillId) override;
	virtual void OnAttack(const struct FSkillTableRow* Skill) override;
	virtual void OnDamaged(int32_t Damage, int32_t RemainHP) override;
	bool IsWeaponDrawn() const { return bWeaponDrawn; }

protected:
	void DrawWeapon();
	void RestartSheatheTimer();
	void BeginSheathe();
	void FinishSheathe();
	void ResetWeaponToBack();

	bool bCanSheathe = false;
	bool bWeaponDrawn = false;
	float SheatheDelaySec = 0.f;
	float SheatheSwapSec = 0.f;
	UPROPERTY(Transient) TObjectPtr<UAnimMontage> SheatheMontage;
	FTimerHandle SheatheTimer;
	FTimerHandle SheatheSwapTimer;

};
