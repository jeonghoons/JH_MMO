#include "Game/JMObjectManager.h"
#include "JMGameInstance.h"
#include "Network/NetworkManager.h"
#include "Player/JMMyPlayer.h"
#include "Player/JMPlayer.h"
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

void UJMObjectManager::ClearObjects()
{
	Objects.Empty();
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

	FVector SpawnLoc(ObjInfo.position().x(), ObjInfo.position().y(), ObjInfo.position().z());
	FRotator SpawnRot(0, ObjInfo.position().yaw(), 0);

	AJMCharacterBase* SpawnedActor = nullptr;

	if (ObjectId == NetManager->NetworkId)
	{
		if (GameInst->MyPlayerClass)
		{
			AJMMyPlayer* MyCharacter = World->SpawnActor<AJMMyPlayer>(GameInst->MyPlayerClass, SpawnLoc, SpawnRot);
			MyPlayer = MyCharacter;
			SpawnedActor = MyCharacter;

			if (AJMPlayerController* PC = Cast<AJMPlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
			{
				PC->Possess(MyCharacter);
			}
		}
	}
	else if (ObjInfo.player_type() == Protocol::PLAYER_TYPE_MONSTER)
	{
		if (GameInst->NpcCharacterClass)
		{
			SpawnedActor = World->SpawnActor<ANpcCharacter>(GameInst->NpcCharacterClass, SpawnLoc, SpawnRot);
		}
	}
	else
	{
		if (GameInst->PlayerClass)
		{
			SpawnedActor = World->SpawnActor<AJMPlayer>(GameInst->PlayerClass, SpawnLoc, SpawnRot);
		}
	}

	if (SpawnedActor)
	{
		SpawnedActor->SetPlayerData(ObjInfo);
		Objects.Add(ObjectId, SpawnedActor);

		if (SpawnedActor == MyPlayer)
		{
			if (UCombatHudWidget* Hud = GetCombatHud())
			{
				Hud->SetHero(static_cast<int32>(ObjInfo.player_type()));
				Hud->SetHp(ObjInfo.stat().hp(), ObjInfo.stat().max_hp());
			}
		}
	}
}

UCombatHudWidget* UJMObjectManager::GetCombatHud() const
{
	UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr;
	return UIManager ? Cast<UCombatHudWidget>(UIManager->GetWidget(WidgetNames::HUD)) : nullptr;
}

void UJMObjectManager::HandleDespawn(int32 ObjectId)
{
	if (TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(ObjectId))
	{
		if (*FoundActor != nullptr)
		{
			(*FoundActor)->Destroy();
		}
		Objects.Remove(ObjectId);
	}
}

void UJMObjectManager::HandleMove(const Protocol::ObjectInfo& ObjInfo)
{
	int ObjectId = ObjInfo.id();
	if (TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(ObjectId))
	{
		if (*FoundActor == nullptr) return;
		if (*FoundActor == MyPlayer) return; 

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
		if (Attacker != MyPlayer)
		{
			Attacker->FaceYaw(AimYaw);
		}
		if (Skill) Attacker->LockFacingForSkill(*Skill);

		Attacker->OnAttack(Skill);

		if (Skill) SpawnSkillFx(Attacker, *Skill, AimYaw, AimX, AimY);

		// 내 시전만 HUD 쿨타임에 반영한다. 길이는 서버가 적용한 값이다.
		if (Skill && Attacker == MyPlayer)
		{
			if (UCombatHudWidget* Hud = GetCombatHud()) Hud->StartCooldown(Skill->Slot, CooldownMs);
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


void UJMObjectManager::HandleDead(int32 CharacterId)
{
	if (TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(CharacterId))
	{
		if (*FoundActor == nullptr) return;

		(*FoundActor)->OnDead();

		if (*FoundActor == MyPlayer)
		{
			if (UCombatHudWidget* Hud = GetCombatHud()) Hud->SetHp(0, MyPlayer->GetObjectInfo().stat().max_hp());
		}
	}
}
