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
	}
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

void UJMObjectManager::HandleAttack(int32 AttackerId, int32 SkillId, float AimYaw, float AimX, float AimY)
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

		Attacker->OnAttack();

		// 지면 장판: 서버가 사거리로 보정한 지점에 지속시간 동안 이펙트를 재생한다. 이펙트가 없으면 디버그 원으로 표시한다.
		if (Skill && Skill->HitShape == ESkillHitShape::Zone && Skill->DurationMs > 0)
		{
			const float FeetZ = Attacker->GetActorLocation().Z - Attacker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f;
			const FVector Center(AimX, AimY, FeetZ);
			const float DurationSec = Skill->DurationMs / 1000.f;

			UParticleSystem* ZoneFx = Skill->ZoneFx.IsNull() ? nullptr : Skill->ZoneFx.LoadSynchronous();
			const float FxScale = Skill->ZoneFxBaseRadius > 0.f ? Skill->Radius / Skill->ZoneFxBaseRadius : 1.f;
			UParticleSystemComponent* FxComp = ZoneFx ? UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), ZoneFx, Center, FRotator::ZeroRotator, FVector(FxScale)) : nullptr;
			if (FxComp)
			{
				// 반복 재생 이펙트도 지속시간이 끝나면 멈춘다. 비활성화 후 남은 파티클이 사라지면 자동 삭제된다.
				FTimerHandle Handle;
				GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(FxComp, [FxComp]() { FxComp->Deactivate(); }), DurationSec, false);
			}
			else
			{
				if (!Skill->ZoneFx.IsNull())
				{
					UE_LOG(LogTemp, Warning, TEXT("HandleAttack: ZoneFx load failed (%s)"), *Skill->ZoneFx.ToString());
				}
				DrawDebugCircle(GetWorld(), Center, Skill->Radius, 48, FColor(255, 80, 80), false, DurationSec, 0, 4.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
			}
		}
	}
}

void UJMObjectManager::HandleDamage(int32 AttackerId, int32 TargetId, int32 Damage, int32 RemainHp)
{
	if (TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(TargetId))
	{
		if (*FoundActor == nullptr) return;

		(*FoundActor)->OnDamaged(Damage, RemainHp);
	}
}


void UJMObjectManager::HandleDead(int32 CharacterId)
{
	if (TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(CharacterId))
	{
		if (*FoundActor == nullptr) return;

		(*FoundActor)->OnDead();
	}
}
