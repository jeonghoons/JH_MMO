#include "Game/ProjectileFx.h"
#include "Character/JMCharacterBase.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

namespace
{
	// 활을 쥔 손의 무기 소켓(D-248). 소켓이 없으면 몸 가운데에서 쏜다.
	const FName BowSocketName(TEXT("WeaponSocket_L"));
	// 대상의 몸 가운데까지 이 거리 안이면 닿은 것으로 본다.
	constexpr float ArriveDistCm = 40.f;
	// 끈 뒤 리본 꼬리가 사라질 때까지 남겨 두는 시간
	constexpr float FadeOutSec = 0.5f;
}

AProjectileFx::AProjectileFx()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	SetCanBeDamaged(false);
}

AProjectileFx* AProjectileFx::Launch(AJMCharacterBase* Attacker, AJMCharacterBase* Target, UParticleSystem* Fx, float AimYaw, float Speed, float MaxRange)
{
	if (Attacker == nullptr || Speed <= 0.f) return nullptr;
	UWorld* World = Attacker->GetWorld();
	if (World == nullptr) return nullptr;

	FVector Start = Attacker->GetActorLocation();
	if (USkeletalMeshComponent* Mesh = Attacker->GetMesh())
	{
		if (Mesh->DoesSocketExist(BowSocketName)) Start = Mesh->GetSocketLocation(BowSocketName);
	}

	FVector Dir = FRotator(0.f, AimYaw, 0.f).Vector();
	if (Target)
	{
		const FVector ToTarget = Target->GetActorLocation() - Start;
		if (!ToTarget.IsNearlyZero()) Dir = ToTarget.GetSafeNormal();
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AProjectileFx* Proj = World->SpawnActor<AProjectileFx>(AProjectileFx::StaticClass(), Start, Dir.Rotation(), Params);
	if (Proj == nullptr) return nullptr;

	Proj->Target = Target;
	Proj->Direction = Dir;
	Proj->Speed = Speed;
	Proj->RemainDist = MaxRange;
	if (Fx)
	{
		Proj->FxComp = UGameplayStatics::SpawnEmitterAttached(Fx, Proj->GetRootComponent(), NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, /*bAutoDestroy*/ false);
	}
	return Proj;
}

void AProjectileFx::AttachMesh(UStaticMesh* Mesh, float StartScale, float EndScale, float GrowSec)
{
	if (Mesh == nullptr) return;
	MeshComp = NewObject<UStaticMeshComponent>(this, TEXT("ProjMesh"));
	MeshComp->SetStaticMesh(Mesh);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComp->SetCastShadow(false);
	MeshComp->SetupAttachment(GetRootComponent());
	MeshComp->RegisterComponent();
	MeshStartScale = StartScale;
	MeshEndScale = EndScale;
	MeshGrowSec = FMath::Max(0.f, GrowSec);
	MeshComp->SetRelativeScale3D(FVector(MeshGrowSec > 0.f ? MeshStartScale : MeshEndScale));
}

void AProjectileFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFinished) return;

	// 메시 크기: 나가는 순간 일반 화살 크기에서 판정 폭에 맞는 크기로 빠르게 커진다(D-278)
	Age += DeltaSeconds;
	if (MeshComp && MeshGrowSec > 0.f)
	{
		const float Alpha = FMath::Clamp(Age / MeshGrowSec, 0.f, 1.f);
		MeshComp->SetRelativeScale3D(FVector(FMath::Lerp(MeshStartScale, MeshEndScale, Alpha)));
	}

	const FVector Here = GetActorLocation();
	const float Step = Speed * DeltaSeconds;

	// 대상이 살아 있으면 지금 위치를 향해 방향을 고친다(대상이 움직여도 따라감 — 서버 판정과 무관한 연출).
	if (AJMCharacterBase* TargetActor = Target.Get())
	{
		const FVector ToTarget = TargetActor->GetActorLocation() - Here;
		const float Dist = ToTarget.Size();
		if (Dist <= FMath::Max(Step, ArriveDistCm))
		{
			SetActorLocation(TargetActor->GetActorLocation());
			Finish();
			return;
		}
		Direction = ToTarget / Dist;
	}

	SetActorLocationAndRotation(Here + Direction * Step, Direction.Rotation());
	RemainDist -= Step;
	if (RemainDist <= 0.f) Finish();
}

void AProjectileFx::Finish()
{
	if (bFinished) return;
	bFinished = true;
	SetActorTickEnabled(false);
	if (FxComp) FxComp->DeactivateSystem();
	if (MeshComp) MeshComp->SetVisibility(false);
	SetLifeSpan(FadeOutSec);
}
