#include "Player/JMPlayer.h"
#include "Game/JMGameInstance.h"
#include "Character/EquipmentComponent.h"
#include "Game/DataManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "TimerManager.h"
#include "Data/SkillTableRow.h"

AJMPlayer::AJMPlayer()
{
	Equipment = CreateDefaultSubobject<UEquipmentComponent>(TEXT("Equipment"));
}

EWeaponType AJMPlayer::GetWeaponType() const
{
	const int32 WeaponIndex = static_cast<int32>(Protocol::EQUIP_PART_WEAPON) - 1;   // equip_items는 슬롯 순서(EquipPart - 1)
	const int32 WeaponId = WeaponIndex < ObjectInfo.equip_items_size() ? ObjectInfo.equip_items(WeaponIndex) : 0;
	if (WeaponId == 0) return EWeaponType::Unarmed;

	const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FItemTableRow* Item = DataManager ? DataManager->FindItemById(WeaponId) : nullptr;
	return (Item && Item->WeaponType != EWeaponType::None) ? Item->WeaponType : EWeaponType::Unarmed;
}

void AJMPlayer::ApplyEquipChange(Protocol::EquipPart Part, int32 ItemId, const Protocol::StatInfo& Stat)
{
	const int32 Index = static_cast<int32>(Part) - 1;
	if (Index < 0 || Index >= 6) return;
	while (ObjectInfo.equip_items_size() < 6) ObjectInfo.add_equip_items(0);
	ObjectInfo.set_equip_items(Index, ItemId);

	// 무기가 바뀌면 새 무기는 등에 멘 상태로 시작한다(넣기 값은 외형 로드 뒤 SetSheatheData로 온다)
	if (Part == Protocol::EQUIP_PART_WEAPON) ResetWeaponToBack();

	ObjectInfo.mutable_stat()->CopyFrom(Stat);
	RefreshMoveSpeed();   // 스탯 × 조준 배율 (방어구 교체는 조준을 풀지 않는다)

	if (Equipment) Equipment->SetSlotItem(Index, ItemId);
}

int32 AJMPlayer::GetWeaponStance() const
{
	if (IsSkillAiming()) return 2;
	return (!bCanSheathe || bWeaponDrawn) ? 1 : 0;
}

void AJMPlayer::SetSheatheData(bool bInCanSheathe, float DelaySec, UAnimMontage* Montage, float SwapSec)
{
	bCanSheathe = bInCanSheathe;
	SheatheDelaySec = DelaySec;
	SheatheMontage = Montage;
	SheatheSwapSec = SwapSec;
	// 로드가 늦게 끝나는 사이 조준·공격으로 이미 꺼냈으면 그대로 둔다
	if (bCanSheathe && (bWeaponDrawn || IsSkillAiming()))
	{
		bWeaponDrawn = false;
		DrawWeapon();
		if (!IsSkillAiming()) RestartSheatheTimer();
	}
}

void AJMPlayer::SetSkillAiming(int32 SkillId)
{
	const bool bWasAiming = IsSkillAiming();
	Super::SetSkillAiming(SkillId);
	if (SkillId != 0)
	{
		DrawWeapon();
		GetWorldTimerManager().ClearTimer(SheatheTimer);
	}
	else if (bWasAiming && bWeaponDrawn)
	{
		RestartSheatheTimer();
	}
}

void AJMPlayer::OnAttack(const FSkillTableRow* Skill)
{
	DrawWeapon();   // 몽타주보다 먼저 손으로(꺼내는 모션 없이 — 서버 타격 시점을 늦추지 않는다)
	Super::OnAttack(Skill);
	if (!IsSkillAiming()) RestartSheatheTimer();
}

void AJMPlayer::OnDamaged(int32_t Damage, int32_t RemainHP)
{
	Super::OnDamaged(Damage, RemainHP);
	if (bWeaponDrawn && !IsSkillAiming()) RestartSheatheTimer();
}

void AJMPlayer::DrawWeapon()
{
	if (!bCanSheathe) return;
	GetWorldTimerManager().ClearTimer(SheatheSwapTimer);
	if (SheatheMontage)
	{
		if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
			if (Anim->Montage_IsPlaying(SheatheMontage)) Anim->Montage_Stop(0.1f, SheatheMontage);
	}
	if (bWeaponDrawn) return;
	bWeaponDrawn = true;
	if (Equipment) Equipment->SetWeaponOnBack(false);
}

void AJMPlayer::RestartSheatheTimer()
{
	if (!bCanSheathe || !bWeaponDrawn || SheatheDelaySec <= 0.f) return;
	GetWorldTimerManager().SetTimer(SheatheTimer, this, &AJMPlayer::BeginSheathe, SheatheDelaySec, false);
}

void AJMPlayer::BeginSheathe()
{
	if (!bWeaponDrawn || IsSkillAiming() || IsDeadActor()) return;
	UAnimInstance* Anim = (SheatheMontage && GetMesh()) ? GetMesh()->GetAnimInstance() : nullptr;
	if (Anim && Anim->Montage_Play(SheatheMontage) > 0.f && SheatheSwapSec > 0.f)
	{
		// 손이 등에 닿는 순간 소켓을 바꾼다(그 전에 공격·조준하면 DrawWeapon이 취소)
		GetWorldTimerManager().SetTimer(SheatheSwapTimer, this, &AJMPlayer::FinishSheathe, SheatheSwapSec, false);
		return;
	}
	FinishSheathe();
}

void AJMPlayer::FinishSheathe()
{
	if (!bWeaponDrawn) return;
	bWeaponDrawn = false;
	if (Equipment) Equipment->SetWeaponOnBack(true);
}

void AJMPlayer::ResetWeaponToBack()
{
	GetWorldTimerManager().ClearTimer(SheatheTimer);
	GetWorldTimerManager().ClearTimer(SheatheSwapTimer);
	bWeaponDrawn = false;
	bCanSheathe = false;   // 새 무기의 값은 SetSheatheData가 다시 채운다
	if (Equipment) Equipment->SetWeaponOnBack(true);
}

void AJMPlayer::SetPlayerData(const Protocol::ObjectInfo& ObjInfo)
{
	Super::SetPlayerData(ObjInfo);
	RefreshEquipment();
}

void AJMPlayer::RefreshEquipment()
{
	if (Equipment == nullptr) return;
	// equip_items는 슬롯 순서(EquipPart - 1), 빈 슬롯 0. 체형은 가입 때 고정(1 MALE, 2 FEMALE)
	TArray<int32> Items;
	for (int32 i = 0; i < ObjectInfo.equip_items_size(); ++i) Items.Add(ObjectInfo.equip_items(i));
	Equipment->SetLoadout(static_cast<int32>(ObjectInfo.player_type()), Items);
}
