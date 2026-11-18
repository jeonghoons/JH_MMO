#include "Game/JMObjectManager.h"
#include "Game/JMGameInstance.h"
#include "Network/NetworkManager.h"
#include "Player/JMMyPlayer.h"
#include "Player/JMPlayer.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Character/NpcCharacter.h"
#include "Engine/World.h"
#include "Player/JMPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Game/DataManager.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Particles/ParticleSystemComponent.h"
#include "TimerManager.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"
#include "UI/CombatHudWidget.h"
#include "UI/ReviveWidget.h"
#include "Game/DropItemActor.h"

void UJMObjectManager::ClearObjects()
{
	Objects.Empty();
	Drops.Empty();
	Npcs.Empty();
	MyPlayer = nullptr;
}

void UJMObjectManager::HandleSpawn(const Protocol::ObjectInfo& ObjInfo)
{
	UWorld* World = GetWorld();
	if (World == nullptr) return;

	int ObjectId = ObjInfo.id();
	if (Objects.Contains(ObjectId)) return;

	UJMGameInstance* GameInst = Cast<UJMGameInstance>(GetGameInstance());
	UNetworkManager* NetManager = GetGameInstance()->GetSubsystem<UNetworkManager>();
	if (GameInst == nullptr || NetManager == nullptr) return;

	// 바닥 드랍(OBJECT_TYPE_ITEM, 서버 A3.4)은 캐릭터가 아니다. 표시 액터를 만들어 드랍 전용 맵에 둔다 (A6.2).
	// template_id = ItemTable ID(0 = 골드), item_count = 수량. 줍기는 AJMMyPlayer가 거리로 골라 요청한다.
	if (ObjInfo.object_type() == Protocol::OBJECT_TYPE_ITEM)
	{
		if (Drops.Contains(ObjectId)) return;
		FActorSpawnParameters DropParams;
		DropParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		UClass* DropClass = GameInst->DropItemClass ? GameInst->DropItemClass.Get() : ADropItemActor::StaticClass();
		const FVector DropLoc(ObjInfo.position().x(), ObjInfo.position().y(), ObjInfo.position().z());
		if (ADropItemActor* Drop = World->SpawnActor<ADropItemActor>(DropClass, DropLoc, FRotator::ZeroRotator, DropParams))
		{
			Drop->InitDrop(ObjectId, ObjInfo.template_id(), ObjInfo.item_count());
			Drops.Add(ObjectId, Drop);
		}
		return;
	}

	FVector SpawnLoc(ObjInfo.position().x(), ObjInfo.position().y(), ObjInfo.position().z());
	FRotator SpawnRot(0, ObjInfo.position().yaw(), 0);

	// 마을 NPC(OBJECT_TYPE_NPC, L4): 플레이어 액터 클래스로 만들어 서버가 준 체형(player_type)·장비 외형(equip_items)을 입힌다.
	// 움직이지 않고 대상이 되지 않으므로 Objects가 아니라 Npcs에 둔다. 상호작용은 AJMMyPlayer가 거리로 골라 F 키로 한다.
	if (ObjInfo.object_type() == Protocol::OBJECT_TYPE_NPC)
	{
		if (Npcs.Contains(ObjectId) || !GameInst->PlayerClass) return;
		FActorSpawnParameters NpcParams;
		NpcParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		// 서버가 주는 NPC 높이는 지면(내비메시)이다. 플레이어 액터는 캡슐 중심 높이를 쓰므로 반높이만큼 올린다(그대로 두면 허리까지 묻힌다).
		const ACharacter* DefaultCharacter = GameInst->PlayerClass->GetDefaultObject<ACharacter>();
		const float HalfHeight = (DefaultCharacter && DefaultCharacter->GetCapsuleComponent()) ? DefaultCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
		Protocol::ObjectInfo NpcInfo = ObjInfo;
		NpcInfo.mutable_position()->set_z(ObjInfo.position().z() + HalfHeight);
		const FVector NpcLoc(SpawnLoc.X, SpawnLoc.Y, SpawnLoc.Z + HalfHeight);
		if (AJMPlayer* Npc = World->SpawnActor<AJMPlayer>(GameInst->PlayerClass, NpcLoc, SpawnRot, NpcParams))
		{
			Npc->SetPlayerData(NpcInfo);
			Npcs.Add(ObjectId, Npc);
		}
		return;
	}

	AJMCharacterBase* SpawnedActor = nullptr;

	// 서버가 있다고 한 오브젝트는 반드시 만든다. 서버 z는 바닥 높이라 캡슐이 지형과 겹쳐 기본 설정(충돌 시 생성 취소)이면
	// 몬스터·다른 플레이어가 보이지 않는다(2026-10-30 S1 PIE에서 발견). 겹치면 밀어 올리고, 그래도 안 되면 그 자리에 만든다.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	if (ObjectId == NetManager->NetworkId)
	{
		if (GameInst->MyPlayerClass)
		{
			AJMMyPlayer* MyCharacter = World->SpawnActor<AJMMyPlayer>(GameInst->MyPlayerClass, SpawnLoc, SpawnRot, SpawnParams);
			MyPlayer = MyCharacter;
			SpawnedActor = MyCharacter;

			if (AJMPlayerController* PC = Cast<AJMPlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
			{
				PC->Possess(MyCharacter);
			}
		}
	}
	// 몬스터는 ObjectType으로 구분한다(PlayerType은 플레이어 체형 전용). 몬스터 종류(template_id)별 외형은 몬스터가 늘 때 추가한다.
	else if (ObjInfo.object_type() == Protocol::OBJECT_TYPE_MONSTER)
	{
		if (GameInst->NpcCharacterClass)
		{
			SpawnedActor = World->SpawnActor<ANpcCharacter>(GameInst->NpcCharacterClass, SpawnLoc, SpawnRot, SpawnParams);
		}
	}
	else
	{
		if (GameInst->PlayerClass)
		{
			SpawnedActor = World->SpawnActor<AJMPlayer>(GameInst->PlayerClass, SpawnLoc, SpawnRot, SpawnParams);
		}
	}

	if (SpawnedActor)
	{
		SpawnedActor->SetPlayerData(ObjInfo);
		Objects.Add(ObjectId, SpawnedActor);

		// 이미 죽어 있는 플레이어가 시야에 들어왔다(사망 상태 = hp 0, D-149). 바로 사망 자세로 둔다.
		if (ObjInfo.object_type() == Protocol::OBJECT_TYPE_PLAYER && ObjInfo.stat().hp() <= 0 && SpawnedActor != MyPlayer)
			SpawnedActor->EnterDeadPose();

		if (SpawnedActor == MyPlayer)
		{
			MyPlayer->RebuildQuickSlots();
			if (UCombatHudWidget* Hud = GetCombatHud())
			{
				Hud->SetIdentity(UTF8_TO_TCHAR(ObjInfo.name().c_str()), ObjInfo.level());
				Hud->SetQuickSlots(MyPlayer->GetQuickSlots(), MyPlayer->GetWeaponType());
				Hud->SetHp(ObjInfo.stat().hp(), ObjInfo.stat().max_hp());
			}

			// 입장 완료. AJMPlayerController::BeginPlay가 띄운 "방에 입장하는 중..."을 닫는다.
			if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>())
			{
				UIManager->CloseWaitingMessage();
			}
		}
	}
}

UCombatHudWidget* UJMObjectManager::GetCombatHud() const
{
	UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr;
	return UIManager ? Cast<UCombatHudWidget>(UIManager->GetWidget(WidgetNames::HUD)) : nullptr;
}

ADropItemActor* UJMObjectManager::FindNearestDrop(const FVector& From, float RangeCm) const
{
	ADropItemActor* Nearest = nullptr;
	float BestSq = RangeCm * RangeCm;
	for (const TPair<int32, TObjectPtr<ADropItemActor>>& Pair : Drops)
	{
		ADropItemActor* Drop = Pair.Value.Get();
		if (!IsValid(Drop)) continue;
		const float DistSq = FVector::DistSquared2D(From, Drop->GetActorLocation());
		if (DistSq <= BestSq)
		{
			BestSq = DistSq;
			Nearest = Drop;
		}
	}
	return Nearest;
}

int32 UJMObjectManager::FindNearestNpc(const FVector& From, float RangeCm, float& OutDistSq) const
{
	int32 NearestId = 0;
	float BestSq = RangeCm * RangeCm;
	for (const TPair<int32, TObjectPtr<AJMCharacterBase>>& Pair : Npcs)
	{
		const AJMCharacterBase* Npc = Pair.Value.Get();
		if (!IsValid(Npc)) continue;
		// 캐릭터 액터의 위치는 캡슐 중심이지만 수평 거리만 보므로 서버 위치(발밑)와 같은 값이다.
		const float DistSq = FVector::DistSquared2D(From, Npc->GetActorLocation());
		if (DistSq <= BestSq)
		{
			BestSq = DistSq;
			NearestId = Pair.Key;
		}
	}
	OutDistSq = BestSq;
	return NearestId;
}

FString UJMObjectManager::GetNpcName(int32 NpcId) const
{
	const TObjectPtr<AJMCharacterBase>* Found = Npcs.Find(NpcId);
	return (Found && *Found) ? FString(UTF8_TO_TCHAR((*Found)->GetObjectInfo().name().c_str())) : FString();
}

bool UJMObjectManager::GetNpcLocation(int32 NpcId, FVector& OutLocation) const
{
	const TObjectPtr<AJMCharacterBase>* Found = Npcs.Find(NpcId);
	if (Found == nullptr || !IsValid(Found->Get())) return false;
	OutLocation = (*Found)->GetActorLocation();
	return true;
}

void UJMObjectManager::HandleDespawn(int32 ObjectId)
{
	// 마을 NPC: 내 시야에서 벗어났다(L4).
	if (TObjectPtr<AJMCharacterBase>* FoundNpc = Npcs.Find(ObjectId))
	{
		if (*FoundNpc != nullptr)
		{
			(*FoundNpc)->Destroy();
		}
		Npcs.Remove(ObjectId);
		return;
	}
	// 바닥 드랍: 누가 주웠거나 수명이 다했거나 내 시야에서 벗어났다.
	if (TObjectPtr<ADropItemActor>* FoundDrop = Drops.Find(ObjectId))
	{
		if (*FoundDrop != nullptr)
		{
			(*FoundDrop)->Destroy();
		}
		Drops.Remove(ObjectId);
		return;
	}
	if (TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(ObjectId))
	{
		if (*FoundActor != nullptr)
		{
			(*FoundActor)->Destroy();
		}
		Objects.Remove(ObjectId);
	}
}

void UJMObjectManager::HandleMove(const Protocol::ObjectInfo& ObjInfo, uint32 CorrectionSeq)
{
	int ObjectId = ObjInfo.id();
	if (TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(ObjectId))
	{
		if (*FoundActor == nullptr) return;
		if (*FoundActor == MyPlayer)
		{
			// 서버가 내 이동을 거부했다. 서버 위치로 돌아가고 이후 이동에 새 순번을 싣는다. (서버 권위 이동, S1)
			MyPlayer->ApplyServerCorrection(ObjInfo.position(), CorrectionSeq);
			return;
		}

		(*FoundActor)->SetDestInfo(ObjInfo.position());
	}
}

void UJMObjectManager::HandleAttack(int32 AttackerId, int32 SkillId, float AimYaw, float AimX, float AimY, int32 CooldownMs)
{
	if (TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(AttackerId))
	{
		AJMCharacterBase* Attacker = *FoundActor;
		if (Attacker == nullptr) return;

		UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
		const FSkillTableRow* Skill = DataManager ? DataManager->FindSkillById(SkillId) : nullptr;   // 몬스터 스킬은 SkillTable에 없을 수 있다

		// 서버가 확정한 시전 방향으로 돌린다. 내 캐릭터는 입력 시점에 이미 돌렸다.
		// 원격 캐릭터의 스킬 조준은 시전(SC_ATTACK)으로 끝난다(MC4.2d-1). 내 조준은 로컬이 관리한다.
		if (Attacker != MyPlayer)
		{
			Attacker->FaceYaw(AimYaw);
			Attacker->SetSkillAiming(0);
		}
		if (Skill) Attacker->LockFacingForSkill(*Skill);

		Attacker->OnAttack(Skill);

		if (Skill) SpawnSkillFx(Attacker, *Skill, AimYaw, AimX, AimY);

		// 내 시전만 HUD 쿨타임에 반영한다. 길이는 서버가 적용한 값이다.
		if (Skill && Attacker == MyPlayer)
		{
			if (UCombatHudWidget* Hud = GetCombatHud()) Hud->StartCooldown(Skill->Slot, CooldownMs);
			MyPlayer->NoteSkillCooldown(SkillId, CooldownMs);
		}
	}
}

void UJMObjectManager::SpawnSkillFx(AJMCharacterBase* Attacker, const FSkillTableRow& Skill, float AimYaw, float AimX, float AimY)
{
	UWorld* World = GetWorld();
	if (World == nullptr || Attacker == nullptr) return;

	UParticleSystem* Fx = Skill.Fx.IsNull() ? nullptr : Skill.Fx.LoadSynchronous();
	if (Fx == nullptr && !Skill.Fx.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("SpawnSkillFx: Fx load failed (%s)"), *Skill.Fx.ToString());
	}
	// 이펙트 크기는 스킬 범위(Radius) 기준이다. 서버 판정·조준 인디케이터와 같은 값을 쓴다.
	const float FxScale = Skill.FxBaseRadius > 0.f ? Skill.Radius / Skill.FxBaseRadius : 1.f;
	const float FeetZOffset = -Attacker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f;

	// 지면 장판: 서버가 사거리로 보정한 지점에 지속시간 동안 재생한다. 이펙트가 없으면 디버그 원으로 표시한다.
	if (Skill.HitShape == ESkillHitShape::Zone)
	{
		if (Skill.DurationMs <= 0) return;
		const FVector Center(AimX, AimY, Attacker->GetActorLocation().Z + FeetZOffset);
		const float DurationSec = Skill.DurationMs / 1000.f;

		UParticleSystemComponent* FxComp = Fx ? UGameplayStatics::SpawnEmitterAtLocation(World, Fx, Center, FRotator::ZeroRotator, FVector(FxScale)) : nullptr;
		if (FxComp)
		{
			// 반복 재생 이펙트도 지속시간이 끝나면 멈춘다. 비활성화 후 남은 파티클이 사라지면 자동 삭제된다.
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(FxComp, [FxComp]() { FxComp->Deactivate(); }), DurationSec, false);
		}
		else
		{
			DrawDebugCircle(World, Center, Skill.Radius, 48, FColor(255, 80, 80), false, DurationSec, 0, 4.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
		}
		return;
	}

	// 부채꼴·직선: 타격 시점(HitDelayMs)에 시전자 발밑에서 시전 방향으로 한 번 재생한다. 이펙트가 없으면 표시하지 않는다.
	if (Fx == nullptr || (Skill.HitShape != ESkillHitShape::Sector && Skill.HitShape != ESkillHitShape::Line)) return;

	TWeakObjectPtr<AJMCharacterBase> WeakAttacker = Attacker;
	TWeakObjectPtr<UParticleSystem> WeakFx = Fx;
	auto Play = [WeakAttacker, WeakFx, AimYaw, FxScale, FeetZOffset]()
		{
			if (!WeakAttacker.IsValid() || !WeakFx.IsValid()) return;
			const FVector Location = WeakAttacker->GetActorLocation() + FVector(0.f, 0.f, FeetZOffset);
			UGameplayStatics::SpawnEmitterAtLocation(WeakAttacker->GetWorld(), WeakFx.Get(), Location, FRotator(0.f, AimYaw, 0.f), FVector(FxScale));
		};

	if (Skill.HitDelayMs > 0)
	{
		// 서버 판정 시점과 같게 재생 배율(기본 공격은 AttackSpeed)로 나눈다.
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Attacker, Play), Skill.HitDelayMs / 1000.f / Attacker->GetAttackPlayRate(&Skill), false);
	}
	else
	{
		Play();
	}
}

void UJMObjectManager::HandleUseItem(int32 ObjectId, int32 ItemId, int32 Hp, int32 CooldownMs)
{
	TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(ObjectId);
	if (FoundActor == nullptr || *FoundActor == nullptr) return;

	(*FoundActor)->SetServerHp(Hp);
	if (*FoundActor == MyPlayer)
	{
		if (UCombatHudWidget* Hud = GetCombatHud())
		{
			Hud->SetHp(Hp, MyPlayer->GetObjectInfo().stat().max_hp());
			Hud->StartItemCooldown(ItemId, CooldownMs);
		}
	}
}

void UJMObjectManager::HandleChangeHp(int32 ObjectId, int32 Hp)
{
	TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(ObjectId);
	if (FoundActor == nullptr || *FoundActor == nullptr) return;

	(*FoundActor)->SetServerHp(Hp);
	if (*FoundActor == MyPlayer)
	{
		if (UCombatHudWidget* Hud = GetCombatHud()) Hud->SetHp(Hp, MyPlayer->GetObjectInfo().stat().max_hp());
	}
}

void UJMObjectManager::HandleDamage(int32 AttackerId, int32 TargetId, int32 Damage, int32 RemainHp)
{
	if (TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(TargetId))
	{
		if (*FoundActor == nullptr) return;

		(*FoundActor)->OnDamaged(Damage, RemainHp);

		if (*FoundActor == MyPlayer)
		{
			if (UCombatHudWidget* Hud = GetCombatHud()) Hud->SetHp(RemainHp, MyPlayer->GetObjectInfo().stat().max_hp());
		}
	}
}


void UJMObjectManager::HandleChangeEquip(int32 ObjectId, Protocol::EquipPart Part, int32 ItemId, const Protocol::StatInfo& Stat)
{
	TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(ObjectId);
	if (FoundActor == nullptr || *FoundActor == nullptr) return;

	AJMPlayer* Player = Cast<AJMPlayer>(*FoundActor);
	if (Player == nullptr) return;   // 몬스터는 장비가 없다

	// 무기가 바뀌면 스킬 조준이 끝난다(서버도 해제). 내 캐릭터는 끄기를 보내 서버와 맞춘다.
	if (Part == Protocol::EQUIP_PART_WEAPON)
	{
		if (*FoundActor == MyPlayer) MyPlayer->CancelLocalAim();
		else Player->SetSkillAiming(0);
	}

	Player->ApplyEquipChange(Part, ItemId, Stat);

	if (*FoundActor == MyPlayer)
	{
		MyPlayer->RebuildQuickSlots();   // 무기가 바뀌면 칸 N의 스킬도 바뀐다(규칙 배치, D-148)
		if (UCombatHudWidget* Hud = GetCombatHud())
		{
			Hud->SetQuickSlots(MyPlayer->GetQuickSlots(), MyPlayer->GetWeaponType());
			Hud->SetHp(Stat.hp(), Stat.max_hp());
		}
	}
}

void UJMObjectManager::HandleSkillAim(int32 ObjectId, int32 SkillId, bool bActive, uint32 Seq)
{
	TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(ObjectId);
	if (FoundActor == nullptr || *FoundActor == nullptr) return;

	if (*FoundActor == MyPlayer)
	{
		// 켜기 방송(본인 포함)은 이미 로컬에서 켰으므로 무시한다.
		if (!bActive) MyPlayer->OnServerAimOff(Seq);
		return;
	}
	(*FoundActor)->SetSkillAiming(bActive ? SkillId : 0);
}

void UJMObjectManager::HandleDead(int32 CharacterId, int32 ReviveDelayMs)
{
	if (TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(CharacterId))
	{
		if (*FoundActor == nullptr) return;

		(*FoundActor)->OnDead();

		if (*FoundActor == MyPlayer)
		{
			if (UCombatHudWidget* Hud = GetCombatHud()) Hud->SetHp(0, MyPlayer->GetObjectInfo().stat().max_hp());

			// 부활 창: 서버가 알려 준 대기 시간이 지나면 버튼이 켜진다.
			if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>())
			{
				if (UReviveWidget* Revive = Cast<UReviveWidget>(UIManager->GetWidget(WidgetNames::Revive)))
				{
					Revive->StartCountdown(ReviveDelayMs);
					UIManager->SetWidgetVisible(WidgetNames::Revive, true);
				}
			}
		}
	}
}

void UJMObjectManager::HandleRevive(int32 CharacterId, const Protocol::PositionInfo& Pos, int32 Hp, uint32 CorrectionSeq)
{
	TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(CharacterId);
	if (FoundActor == nullptr || *FoundActor == nullptr) return;

	if (*FoundActor != MyPlayer)
	{
		(*FoundActor)->OnRevive(Pos, Hp);
		return;
	}

	MyPlayer->OnMyRevive(Pos, Hp, CorrectionSeq);
	if (UCombatHudWidget* Hud = GetCombatHud()) Hud->SetHp(Hp, MyPlayer->GetObjectInfo().stat().max_hp());
	if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>())
		UIManager->SetWidgetVisible(WidgetNames::Revive, false);
}
