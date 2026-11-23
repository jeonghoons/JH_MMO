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
#include "Game/JMShopManager.h"
#include "Game/JMAuctionManager.h"
#include "Game/JMPartyManager.h"
#include "Game/ProjectileFx.h"
#include "Components/WidgetComponent.h"
#include "UI/NameplateWidget.h"

void UJMObjectManager::ClearObjects()
{
	Objects.Empty();
	Drops.Empty();
	Npcs.Empty();
	Portals.Empty();
	LastAttackSkill.Empty();
	MyPlayer = nullptr;
	// 던전 상태는 Room에 딸린 값이다. 레벨 전환 너머로 넘길 값(TravelCarry·MyReviveReadySec)은 여기서 지우지 않는다(ResetRoomTravel).
	ClearDungeonState();
}

void UJMObjectManager::ResetRoomTravel()
{
	TravelCarry = FRoomTravelCarry();
	MyReviveReadySec = 0.0;
}

void UJMObjectManager::NoteEnterRoomSent()
{
	if (TravelCarry.bActive) TravelCarry.EnterSentSec = FPlatformTime::Seconds();
}

void UJMObjectManager::ClearDungeonState()
{
	DungeonState = Protocol::DUNGEON_STATE_NONE;
	DungeonClearEndSec = 0.0;
	if (UCombatHudWidget* Hud = GetCombatHud()) Hud->SetDungeonState(DungeonState, DungeonClearEndSec);
}

void UJMObjectManager::HandleDungeonState(const Protocol::SC_DUNGEON_STATE_PACKET& Pkt)
{
	DungeonState = Pkt.state();
	// 남은 시간은 클리어일 때만 뜻이 있다(그 밖은 0). 받은 순간부터 클라이언트가 줄여 가며 보여 준다.
	DungeonClearEndSec = (Pkt.state() == Protocol::DUNGEON_STATE_CLEARED) ? FPlatformTime::Seconds() + FMath::Max(Pkt.remain_sec(), 0) : 0.0;
	UE_LOG(LogTemp, Log, TEXT("[Dungeon] state=%d remain=%d sec"), DungeonState, Pkt.remain_sec());
	if (UCombatHudWidget* Hud = GetCombatHud()) Hud->SetDungeonState(DungeonState, DungeonClearEndSec);
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
			float FallFromZ = 0.f;
			if (FindDropFallStart(DropLoc, FallFromZ)) Drop->StartFall(FallFromZ);
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

			// 머리 위 이름표 + 퀘스트 표식(Q1, D-265). 플레이어 액터 클래스를 같이 쓰므로 BP_Player에 넣지 않고 NPC로 만들 때만 붙인다.
			// 화면 공간 위젯이라 거리와 관계없이 같은 크기다(몬스터 HP 막대 ANpcCharacter와 같은 방식).
			if (UWidgetComponent* Nameplate = NewObject<UWidgetComponent>(Npc, TEXT("NameplateComponent")))
			{
				Nameplate->SetupAttachment(Npc->GetRootComponent());
				Nameplate->SetWidgetSpace(EWidgetSpace::Screen);
				Nameplate->SetWidgetClass(UNameplateWidget::StaticClass());
				Nameplate->SetDrawSize(FVector2D(240.f, 60.f));
				Nameplate->SetPivot(FVector2D(0.5f, 1.f));
				Nameplate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				// 캡슐 중심에서 머리 위까지(반높이 + 여유)
				Nameplate->SetRelativeLocation(FVector(0.f, 0.f, HalfHeight + 30.f));
				Nameplate->RegisterComponent();
				Nameplate->InitWidget();
				if (UNameplateWidget* Plate = Cast<UNameplateWidget>(Nameplate->GetUserWidgetObject()))
				{
					Plate->Setup(FString(UTF8_TO_TCHAR(ObjInfo.name().c_str())), ObjInfo.template_id());
				}
			}
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
		if (ANpcCharacter* Monster = Cast<ANpcCharacter>(SpawnedActor))
			Monster->SetTargetIsAlly(IsAllyId(ObjInfo.target_id()));
		Objects.Add(ObjectId, SpawnedActor);

		// 이미 죽어 있는 플레이어가 시야에 들어왔다(사망 상태 = hp 0, D-149). 바로 사망 자세로 둔다.
		// 내 캐릭터도 죽은 채 다른 레벨에 도착할 수 있다(파티장을 따라 던전에 입장, 죽은 채 마을로 내보내짐 — DG3c-2).
		const bool bSpawnedDead = ObjInfo.object_type() == Protocol::OBJECT_TYPE_PLAYER && ObjInfo.stat().hp() <= 0;
		if (bSpawnedDead)
			SpawnedActor->EnterDeadPose();

		if (SpawnedActor == MyPlayer)
		{
			MyPlayer->RebuildQuickSlots();
			if (UCombatHudWidget* Hud = GetCombatHud())
			{
				Hud->SetIdentity(UTF8_TO_TCHAR(ObjInfo.name().c_str()), ObjInfo.level());
				Hud->SetQuickSlots(MyPlayer->GetQuickSlots(), MyPlayer->GetWeaponType());
				Hud->SetHp(ObjInfo.stat().hp(), ObjInfo.stat().max_hp());
				Hud->SetMp(ObjInfo.stat().mp(), ObjInfo.stat().max_mp());
				Hud->SetExp(ObjInfo.exp(), ObjInfo.level());
			}

			UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();

			// 레벨이 바뀌는 Room 이동으로 온 것이면 옛 액터에서 꺼내 둔 순번·쿨타임을 넣는다(로그인 첫 입장은 서버도 0부터라 넣을 것이 없다).
			if (TravelCarry.bActive)
			{
				MyPlayer->ApplyTravelState(TravelCarry.CorrectionSeq, TravelCarry.AttackSeq, TravelCarry.AimSeq, TravelCarry.SkillCooldownEnd);

				// 로딩 시간(완료 기준의 측정값): 서버는 SC_ROOM_CHANGE부터 CS_ENTER_ROOM까지 LoadTimeoutSec(60초)을 기다린다.
				const double Now = FPlatformTime::Seconds();
				const double EnterSent = TravelCarry.EnterSentSec > 0.0 ? TravelCarry.EnterSentSec : Now;
				UE_LOG(LogTemp, Log, TEXT("[Travel] %s: recv->enter %.0f ms, enter->spawn %.0f ms, total %.0f ms (seq=%u, dead=%d)"),
					*TravelCarry.MapName, (EnterSent - TravelCarry.RecvSec) * 1000.0, (Now - EnterSent) * 1000.0, (Now - TravelCarry.RecvSec) * 1000.0,
					TravelCarry.CorrectionSeq, bSpawnedDead ? 1 : 0);
				TravelCarry = FRoomTravelCarry();
			}

			if (bSpawnedDead)
			{
				// 서버는 SC_DEAD를 다시 보내지 않는다. 죽을 때 적어 둔 시각으로 부활 창의 남은 시간을 맞춘다(지났으면 바로 누를 수 있음).
				if (UReviveWidget* Revive = UIManager ? Cast<UReviveWidget>(UIManager->GetWidget(WidgetNames::Revive)) : nullptr)
				{
					const double RemainSec = MyReviveReadySec > 0.0 ? MyReviveReadySec - FPlatformTime::Seconds() : 0.0;
					Revive->StartCountdown(FMath::CeilToInt32(FMath::Max(RemainSec, 0.0) * 1000.0));
					UIManager->SetWidgetVisible(WidgetNames::Revive, true);
				}
			}
			else
			{
				MyReviveReadySec = 0.0;
			}

			// 입장 완료. AJMPlayerController::BeginPlay가 띄운 대기 창("방에 입장하는 중..."·"이동하는 중...")을 닫는다.
			if (UIManager)
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

int32 UJMObjectManager::FindNearestNpc(const FVector& From, float RangeCm, float& OutDistSq, int32 KindFilter) const
{
	int32 NearestId = 0;
	float BestSq = RangeCm * RangeCm;
	for (const TPair<int32, TObjectPtr<AJMCharacterBase>>& Pair : Npcs)
	{
		const AJMCharacterBase* Npc = Pair.Value.Get();
		if (!IsValid(Npc)) continue;
		if (KindFilter != 0 && static_cast<int32>(Npc->GetObjectInfo().npc_kind()) != KindFilter) continue;
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

int32 UJMObjectManager::GetNpcKind(int32 NpcId) const
{
	const TObjectPtr<AJMCharacterBase>* Found = Npcs.Find(NpcId);
	return (Found && *Found) ? static_cast<int32>((*Found)->GetObjectInfo().npc_kind()) : 0;
}

FString UJMObjectManager::GetNpcName(int32 NpcId) const
{
	const TObjectPtr<AJMCharacterBase>* Found = Npcs.Find(NpcId);
	return (Found && *Found) ? FString(UTF8_TO_TCHAR((*Found)->GetObjectInfo().name().c_str())) : FString();
}

int32 UJMObjectManager::GetNpcTemplateId(int32 NpcId) const
{
	const TObjectPtr<AJMCharacterBase>* Found = Npcs.Find(NpcId);
	return (Found && *Found) ? (*Found)->GetObjectInfo().template_id() : 0;
}

FString UJMObjectManager::FindNpcNameByTemplate(int32 TemplateId) const
{
	for (const TPair<int32, TObjectPtr<AJMCharacterBase>>& Pair : Npcs)
	{
		const AJMCharacterBase* Npc = Pair.Value.Get();
		if (IsValid(Npc) && Npc->GetObjectInfo().template_id() == TemplateId) return FString(UTF8_TO_TCHAR(Npc->GetObjectInfo().name().c_str()));
	}
	return FString();
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
		if (ANpcCharacter* Monster = Cast<ANpcCharacter>(*FoundActor))
			Monster->SetTargetIsAlly(IsAllyId(ObjInfo.target_id()));
	}
}

bool UJMObjectManager::FindDropFallStart(const FVector& DropLoc, float& OutZ)
{
	// 서버는 새 드랍과 시야에 새로 들어온 드랍을 같은 SC_ADD_OBJECT로 보낸다. 근처에서 방금 죽은 몬스터(서버 드랍 반경 80cm)나
	// 바로 옆의 플레이어(버리기)가 있으면 새 드랍으로 보고 그 몸 가운데 높이에서 떨어뜨린다. 멀리서 시야에 들어온 드랍은 바닥에 그대로.
	constexpr double DeathWindowSec = 1.5;
	constexpr float MonsterRadiusCm = 200.f;
	constexpr float PlayerRadiusCm = 150.f;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	RecentMonsterDeaths.RemoveAll([Now](const FRecentDeath& D) { return Now - D.Time > DeathWindowSec; });
	for (const FRecentDeath& D : RecentMonsterDeaths)
	{
		if (FVector::DistSquared2D(D.Location, DropLoc) <= FMath::Square(MonsterRadiusCm))
		{
			OutZ = D.Location.Z;
			return true;
		}
	}
	for (const TPair<int32, TObjectPtr<AJMCharacterBase>>& Pair : Objects)
	{
		const AJMCharacterBase* Actor = Pair.Value.Get();
		if (Actor && Actor->GetObjectInfo().object_type() == Protocol::OBJECT_TYPE_PLAYER &&
			FVector::DistSquared2D(Actor->GetActorLocation(), DropLoc) <= FMath::Square(PlayerRadiusCm))
		{
			OutZ = Actor->GetActorLocation().Z;
			return true;
		}
	}
	return false;
}

bool UJMObjectManager::IsAllyId(int32 ObjectId) const
{
	if (ObjectId == 0) return false;
	if (MyPlayer && MyPlayer->GetObjectInfo().id() == ObjectId) return true;
	if (const UJMPartyManager* Party = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMPartyManager>() : nullptr)
	{
		for (const FJMPartyMember& Member : Party->GetMembers())
			if (Member.ObjectId == ObjectId) return true;
	}
	return false;
}

void UJMObjectManager::HandleAttackResult(int32 Result, uint32 Seq, int32 RemainMs)
{
	if (MyPlayer) MyPlayer->RollbackCast(Seq, Result, RemainMs);
}

void UJMObjectManager::HandleAttack(int32 AttackerId, int32 SkillId, int32 TargetId, float AimYaw, float AimX, float AimY, int32 CooldownMs, uint32 Seq)
{
	// 피격 이펙트용(D-254). 내 시전도 SC_ATTACK이 오므로 아래의 확정 return보다 먼저 적는다.
	LastAttackSkill.Add(AttackerId, SkillId);

	if (TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(AttackerId))
	{
		AJMCharacterBase* Attacker = *FoundActor;
		if (Attacker == nullptr) return;

		// 내 시전은 입력 때 이미 몽타주·이펙트·쿨타임을 시작했다(CP1). 확정만 하고 다시 재생하지 않는다.
		// false = 먼저 시작한 것이 없거나(순번 0) 서버가 다른 스킬로 확정했다 → 아래에서 서버 값으로 재생한다.
		// 관통 투사체: 서버가 aim 지점 자리에 벽·지형에 막혀 멈추는 끝점을 싣는다(FP1). 화살 연출이 나갈 때(또는 이미 나갔으면 ConfirmCast가) 쓴다.
		if (UDataManager* DM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr)
		{
			if (const FSkillTableRow* PierceSkill = DM->FindSkillById(SkillId); PierceSkill && PierceSkill->HitShape == ESkillHitShape::Pierce)
				Attacker->SetPierceEnd(FVector2D(AimX, AimY));
		}

		if (Attacker == MyPlayer && MyPlayer->ConfirmCast(Seq, SkillId, AimX, AimY)) return;

		UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
		const FSkillTableRow* Skill = DataManager ? DataManager->FindSkillById(SkillId) : nullptr;   // 몬스터 스킬은 SkillTable에 없을 수 있다

		// 서버가 확정한 시전 방향으로 돌린다. 내 캐릭터는 입력 시점에 이미 돌렸다.
		// 원격 캐릭터의 스킬 조준은 시전(SC_ATTACK)으로 끝난다(MC4.2d-1). 내 조준은 로컬이 관리한다.
		if (Attacker != MyPlayer)
		{
			Attacker->FaceYaw(AimYaw);
			Attacker->SetSkillAiming(0);
		}
		// 몬스터가 노린 대상도 HP바 표시 판단에 쓴다(FP1, D-286)
		if (ANpcCharacter* Monster = Cast<ANpcCharacter>(Attacker))
			Monster->SetTargetIsAlly(IsAllyId(TargetId));
		if (Skill) Attacker->LockFacingForSkill(*Skill);

		Attacker->OnAttack(Skill);

		if (Skill) SpawnSkillFx(Attacker, *Skill, TargetId, AimYaw, AimX, AimY);

		// 내 시전만 HUD 쿨타임에 반영한다. 길이는 서버가 적용한 값이다.
		if (Skill && Attacker == MyPlayer)
		{
			if (UCombatHudWidget* Hud = GetCombatHud()) Hud->StartCooldown(Skill->Slot, CooldownMs);
			MyPlayer->NoteSkillCooldown(SkillId, CooldownMs);
		}
	}
}

void UJMObjectManager::SpawnSkillFx(AJMCharacterBase* Attacker, const FSkillTableRow& Skill, int32 TargetId, float AimYaw, float AimX, float AimY,
	TWeakObjectPtr<UParticleSystemComponent>* OutComp, FTimerHandle* OutTimer, TSharedPtr<TWeakObjectPtr<AActor>> OutProjectile)
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
			if (OutComp) *OutComp = FxComp;
			if (OutTimer) *OutTimer = Handle;
		}
		else
		{
			DrawDebugCircle(World, Center, Skill.Radius, 48, FColor(255, 80, 80), false, DurationSec, 0, 4.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
		}
		return;
	}

	// 투사체(D-254): 타격 시점(= 시위를 놓는 순간)에 화살 연출을 대상 쪽으로 날린다. 판정·피해는 서버가 따로 계산한다(SC_DAMAGE).
	// 느린 관통 투사체(Pierce, D-278): 대상 없이 조준 방향으로 사거리(Range)까지 곧게 날리고, ProjMesh(빛의 화살)가 있으면 싣는다.
	if (Skill.HitShape == ESkillHitShape::Pierce)
	{
		UStaticMesh* Mesh = Skill.ProjMesh.IsNull() ? nullptr : Skill.ProjMesh.LoadSynchronous();
		if ((Fx == nullptr && Mesh == nullptr) || Skill.ProjSpeed <= 0.f) return;

		TWeakObjectPtr<AJMCharacterBase> WeakAttacker = Attacker;
		TWeakObjectPtr<UParticleSystem> WeakFx = Fx;
		TWeakObjectPtr<UStaticMesh> WeakMesh = Mesh;
		const float Speed = Skill.ProjSpeed;
		const float MaxRange = Skill.Range;
		const float StartScale = Skill.ProjStartScale, EndScale = Skill.ProjMeshScale, GrowSec = Skill.ProjGrowMs / 1000.f;
		const float StartFwd = Skill.ProjStartFwdCm, WidthCm = Skill.Width;
		auto Launch = [WeakAttacker, WeakFx, WeakMesh, AimYaw, Speed, MaxRange, StartScale, EndScale, GrowSec, StartFwd, WidthCm, OutProjectile]()
			{
				if (!WeakAttacker.IsValid()) return;
				AProjectileFx* Proj = AProjectileFx::Launch(WeakAttacker.Get(), nullptr, WeakFx.Get(), AimYaw, Speed, MaxRange, StartFwd);
				if (Proj && WeakMesh.IsValid()) Proj->AttachMesh(WeakMesh.Get(), StartScale, EndScale, GrowSec, WidthCm);
				if (Proj && OutProjectile.IsValid()) *OutProjectile = Proj;
			};
		if (Skill.HitDelayMs > 0)
		{
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Attacker, Launch), Skill.HitDelayMs / 1000.f / Attacker->GetAttackPlayRate(&Skill), false);
			if (OutTimer) *OutTimer = Handle;
		}
		else
		{
			Launch();
		}
		return;
	}

	if (Skill.HitShape == ESkillHitShape::Projectile)
	{
		if (Fx == nullptr || Skill.ProjSpeed <= 0.f) return;

		TWeakObjectPtr<AJMCharacterBase> WeakAttacker = Attacker;
		TWeakObjectPtr<AJMCharacterBase> WeakTarget;
		if (TargetId != 0)
		{
			if (TObjectPtr<AJMCharacterBase>* FoundTarget = Objects.Find(TargetId)) WeakTarget = *FoundTarget;
		}
		TWeakObjectPtr<UParticleSystem> WeakFx = Fx;
		const float Speed = Skill.ProjSpeed;
		const float MaxRange = FMath::Max(Skill.Range, Skill.CastRange) * 1.2f + 100.f;
		auto Launch = [WeakAttacker, WeakTarget, WeakFx, AimYaw, Speed, MaxRange, OutProjectile]()
			{
				if (!WeakAttacker.IsValid() || !WeakFx.IsValid()) return;
				AProjectileFx* Proj = AProjectileFx::Launch(WeakAttacker.Get(), WeakTarget.Get(), WeakFx.Get(), AimYaw, Speed, MaxRange);
				if (Proj && OutProjectile.IsValid()) *OutProjectile = Proj;
			};

		if (Skill.HitDelayMs > 0)
		{
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Attacker, Launch), Skill.HitDelayMs / 1000.f / Attacker->GetAttackPlayRate(&Skill), false);
			if (OutTimer) *OutTimer = Handle;
		}
		else
		{
			Launch();
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
			if (OutTimer) *OutTimer = Handle;
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

void UJMObjectManager::HandleGainExp(int64 Exp, int64 Gained)
{
	if (MyPlayer == nullptr) return;
	MyPlayer->SetServerExp(Exp);
	UE_LOG(LogTemp, Log, TEXT("[Growth] exp=%lld (+%lld) level=%d"), Exp, Gained, MyPlayer->GetObjectInfo().level());
	if (UCombatHudWidget* Hud = GetCombatHud()) Hud->SetExp(Exp, MyPlayer->GetObjectInfo().level());
}

void UJMObjectManager::HandleChangeMp(int32 Mp)
{
	if (MyPlayer == nullptr) return;
	MyPlayer->SetServerMp(Mp);
	UE_LOG(LogTemp, Verbose, TEXT("[Growth] mp=%d/%d"), Mp, MyPlayer->GetObjectInfo().stat().max_mp());
	if (UCombatHudWidget* Hud = GetCombatHud()) Hud->SetMp(Mp, MyPlayer->GetObjectInfo().stat().max_mp());
}

void UJMObjectManager::HandleLevelUp(int32 ObjectId, int32 Level, const Protocol::StatInfo& Stat)
{
	TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(ObjectId);
	if (FoundActor == nullptr || *FoundActor == nullptr) return;

	(*FoundActor)->ApplyServerLevel(Level, Stat);
	UE_LOG(LogTemp, Log, TEXT("[Growth] level up object=%d level=%d hp=%d/%d mp=%d/%d atk=%d"), ObjectId, Level,
		Stat.hp(), Stat.max_hp(), Stat.mp(), Stat.max_mp(), Stat.attack_damage());
	if (*FoundActor == MyPlayer)
	{
		if (UCombatHudWidget* Hud = GetCombatHud())
		{
			const Protocol::ObjectInfo& Info = MyPlayer->GetObjectInfo();
			Hud->SetIdentity(UTF8_TO_TCHAR(Info.name().c_str()), Level);
			Hud->SetHp(Stat.hp(), Stat.max_hp());
			Hud->SetMp(Stat.mp(), Stat.max_mp());
			// 뒤따르는 SC_GAIN_EXP가 새 레벨 안의 누적값을 다시 준다. 그 전까지는 지금 저장된 값으로 그린다.
			Hud->SetExp(Info.exp(), Level);
		}
	}
}

void UJMObjectManager::HandleDamage(int32 AttackerId, int32 TargetId, int32 Damage, int32 RemainHp)
{
	if (TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(TargetId))
	{
		if (*FoundActor == nullptr) return;

		(*FoundActor)->OnDamaged(Damage, RemainHp);
		SpawnHitFx(AttackerId, *FoundActor);
		if (ANpcCharacter* Monster = Cast<ANpcCharacter>(*FoundActor); Monster && IsAllyId(AttackerId))
			Monster->OnHitByAlly();

		if (*FoundActor == MyPlayer)
		{
			if (UCombatHudWidget* Hud = GetCombatHud()) Hud->SetHp(RemainHp, MyPlayer->GetObjectInfo().stat().max_hp());
		}
	}
}

void UJMObjectManager::SpawnHitFx(int32 AttackerId, AJMCharacterBase* Target)
{
	if (Target == nullptr) return;
	const int32* SkillId = LastAttackSkill.Find(AttackerId);
	if (SkillId == nullptr) return;

	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FSkillTableRow* Skill = DataManager ? DataManager->FindSkillById(*SkillId) : nullptr;
	if (Skill == nullptr || Skill->HitFx.IsNull()) return;

	UParticleSystem* HitFx = Skill->HitFx.LoadSynchronous();   // 미리 로드됨(UDataManager) — 보통 바로 돌아온다
	if (HitFx == nullptr) return;

	// 몸 가운데(캡슐 중심)에서 공격자 쪽을 보게 재생한다.
	FRotator Facing = Target->GetActorRotation();
	if (TObjectPtr<AJMCharacterBase>* FoundAttacker = Objects.Find(AttackerId))
	{
		if (*FoundAttacker) Facing = (FoundAttacker->Get()->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D().Rotation();
	}
	UGameplayStatics::SpawnEmitterAtLocation(Target->GetWorld(), HitFx, Target->GetActorLocation(), Facing);
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
			Hud->SetMp(Stat.mp(), Stat.max_mp());   // 투구는 최대 MP를 올린다(D-253)
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
		if (Cast<ANpcCharacter>(*FoundActor) && GetWorld())
			RecentMonsterDeaths.Add({ (*FoundActor)->GetActorLocation(), GetWorld()->GetTimeSeconds() });

		if (*FoundActor == MyPlayer)
		{
			if (UCombatHudWidget* Hud = GetCombatHud()) Hud->SetHp(0, MyPlayer->GetObjectInfo().stat().max_hp());

			// 죽은 채 다른 레벨로 옮겨질 때를 위해 부활할 수 있게 되는 시각을 적어 둔다(HandleSpawn이 부활 창을 다시 맞춤).
			MyReviveReadySec = FPlatformTime::Seconds() + FMath::Max(ReviveDelayMs, 0) / 1000.0;

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

void UJMObjectManager::HandleTeleport(int32 CharacterId, const Protocol::PositionInfo& Pos, uint32 CorrectionSeq)
{
	TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(CharacterId);
	if (FoundActor == nullptr || *FoundActor == nullptr) return;

	if (*FoundActor != MyPlayer) (*FoundActor)->TeleportToServerPosition(Pos);
	else MyPlayer->OnMyTeleport(Pos, CorrectionSeq);
}

void UJMObjectManager::HandleRoomChange(const Protocol::SC_ROOM_CHANGE_PACKET& Pkt)
{
	UWorld* World = GetWorld();
	if (World == nullptr) return;

	// 던전 상태는 옛 Room의 것이다. 나갈 때 해제 패킷이 없으므로 여기서 지운다(새 Room이 던전이면 SC_DUNGEON_STATE가 다시 온다).
	ClearDungeonState();
	// 옛 Room 공격자의 마지막 스킬(피격 이펙트용)도 버린다.
	LastAttackSkill.Empty();

	// 레벨 이름은 PIE 접두사를 뺀 짧은 이름으로 비교한다. 레벨 전환이 걸려 있으면(아직 옛 레벨이 열려 있음) 가는 중인 레벨이 기준이다.
	const FString NewMap = UTF8_TO_TCHAR(Pkt.map_name().c_str());
	const FString CurrentMap = TravelCarry.bActive ? TravelCarry.MapName : UGameplayStatics::GetCurrentLevelName(World, true);
	if (!NewMap.Equals(CurrentMap, ESearchCase::IgnoreCase))
	{
		// 다른 레벨(DG3c-2): 서버는 CS_ENTER_ROOM("로딩 끝")이 올 때까지 나를 시야·전투에 넣지 않고 기다린다(LoadTimeoutSec).
		UE_LOG(LogTemp, Log, TEXT("[Room] level change: %s -> %s (kind=%d, seq=%u)"), *CurrentMap, *NewMap, static_cast<int32>(Pkt.kind()), Pkt.correction_seq());

		// 옛 액터는 레벨과 함께 사라진다. 응답 대기 중인 시전·조준을 정리하고, 새 액터에 넘길 순번·쿨타임을 꺼내 둔 뒤 입력을 막는다.
		// (이미 전환 중이라 MyPlayer가 없으면 앞서 꺼내 둔 값을 그대로 쓴다.)
		if (MyPlayer)
		{
			MyPlayer->ResetForRoomChange();
			MyPlayer->GetTravelState(TravelCarry.AttackSeq, TravelCarry.AimSeq, TravelCarry.SkillCooldownEnd);
			MyPlayer->LockForLevelTravel();
		}
		if (UJMShopManager* Shop = GetGameInstance()->GetSubsystem<UJMShopManager>()) Shop->Close();
		if (UJMAuctionManager* Auction = GetGameInstance()->GetSubsystem<UJMAuctionManager>()) { if (Auction->IsAtNpc()) Auction->Close(); }

		TravelCarry.bActive = true;
		TravelCarry.MapName = NewMap;
		TravelCarry.CorrectionSeq = Pkt.correction_seq();
		TravelCarry.RecvSec = FPlatformTime::Seconds();
		TravelCarry.EnterSentSec = 0.0;

		// 오브젝트·위젯 정리와 레벨 열기는 UNetworkManager가 한다(인벤토리·파티 사본은 남김 — 서버가 다시 보내지 않는다).
		if (UNetworkManager* NetManager = GetGameInstance()->GetSubsystem<UNetworkManager>())
			NetManager->TravelToRoomLevel(NewMap);
		return;
	}

	// 같은 레벨인데 내 캐릭터가 아직 없다 = 레벨 전환으로 도착한 직후(스폰 전)에 Room이 또 바뀌었다. 도착 순번만 새 값으로 바꿔 둔다.
	if (TravelCarry.bActive) TravelCarry.CorrectionSeq = Pkt.correction_seq();

	// 응답 대기 중인 내 시전·조준·잠금·상호작용 대상을 먼저 정리한다(부활 이동이면 내 캐릭터는 아직 죽어 있다).
	if (MyPlayer) MyPlayer->ResetForRoomChange();

	// 열려 있는 상점 창은 옛 Room의 NPC를 가리키므로 닫는다. 인벤토리 사본은 서버가 다시 보내지 않으므로 그대로 둔다.
	if (UJMShopManager* Shop = GetGameInstance()->GetSubsystem<UJMShopManager>()) Shop->Close();
	if (UJMAuctionManager* Auction = GetGameInstance()->GetSubsystem<UJMAuctionManager>()) { if (Auction->IsAtNpc()) Auction->Close(); }

	// 내 캐릭터는 Objects에 남긴다. 뒤따르는 본인 SC_ADD_OBJECT는 "이미 있으면 무시"로 넘어가고, SC_REVIVE는 HandleRevive로 간다.
	// 나머지 캐릭터·몬스터는 HandleDespawn과 같은 방식으로 지운다(딸린 위젯은 액터와 함께 사라진다).
	for (auto It = Objects.CreateIterator(); It; ++It)
	{
		if (It->Value == MyPlayer) continue;
		if (It->Value) It->Value->Destroy();
		It.RemoveCurrent();
	}
	for (auto& Pair : Drops)
	{
		if (Pair.Value) Pair.Value->Destroy();
	}
	Drops.Empty();
	for (auto& Pair : Npcs)
	{
		if (Pair.Value) Pair.Value->Destroy();
	}
	Npcs.Empty();
	// 포털 목록은 본인 SC_PORTAL_LIST가 새 Room 것으로 다시 보낸다.
	Portals.Empty();

	// 내 캐릭터를 새 Room의 자리로 옮기고 보정 순번을 받는다(SC_TELEPORT와 같은 절차).
	if (MyPlayer) MyPlayer->OnMyTeleport(Pkt.pos_info(), Pkt.correction_seq());
}

void UJMObjectManager::HandlePortalList(const Protocol::SC_PORTAL_LIST_PACKET& Pkt)
{
	Portals.Reset();
	for (const Protocol::PortalInfo& Info : Pkt.portals())
	{
		FPortalEntry Entry;
		Entry.Id = Info.portal_id();
		Entry.Location = FVector(Info.x(), Info.y(), Info.z());
		Entry.RadiusCm = Info.radius_cm();
		Entry.Kind = static_cast<int32>(Info.kind());
		Portals.Add(Entry);
	}
}

int32 UJMObjectManager::FindNearestPortal(const FVector& From, float& OutDistSq, int32* OutKind) const
{
	int32 NearestId = 0;
	int32 NearestKind = 0;
	float BestSq = TNumericLimits<float>::Max();
	for (const FPortalEntry& Portal : Portals)
	{
		const float DistSq = FVector::DistSquared2D(From, Portal.Location);
		if (DistSq <= Portal.RadiusCm * Portal.RadiusCm && DistSq < BestSq)
		{
			BestSq = DistSq;
			NearestId = Portal.Id;
			NearestKind = Portal.Kind;
		}
	}
	OutDistSq = BestSq;
	if (OutKind) *OutKind = NearestKind;
	return NearestId;
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
	MyReviveReadySec = 0.0;
	if (UCombatHudWidget* Hud = GetCombatHud()) Hud->SetHp(Hp, MyPlayer->GetObjectInfo().stat().max_hp());
	if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>())
		UIManager->SetWidgetVisible(WidgetNames::Revive, false);
}
