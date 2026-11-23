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
	// 지면 따라가기: 지면 위 높이 범위, 높이를 맞추는 속도, 아래로 찾는 깊이
	constexpr float GroundHeightMinCm = 60.f;
	constexpr float GroundHeightMaxCm = 200.f;
	constexpr float GroundFollowInterpSpeed = 12.f;
	constexpr float GroundTraceUpCm = 300.f;
	constexpr float GroundTraceDownCm = 2000.f;
}

AProjectileFx::AProjectileFx()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	SetCanBeDamaged(false);
}

AProjectileFx* AProjectileFx::Launch(AJMCharacterBase* Attacker, AJMCharacterBase* Target, UParticleSystem* Fx, float AimYaw, float Speed, float MaxRange, float StartFwdCm)
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
	// 대상 없는 관통 투사체는 서버 판정선(시전자 중심에서 AimYaw 방향)과 같은 선을 날게 한다 — 손 소켓은 높이만 쓴다(FP1).
	if (Target == nullptr)
	{
		const FVector Center = Attacker->GetActorLocation();
		Start = FVector(Center.X, Center.Y, Start.Z) + Dir * StartFwdCm;
	}
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
	if (Target == nullptr)
	{
		float GroundZ = 0.f;
		if (Proj->TraceGround(Start, GroundZ))
		{
			Proj->bGroundFollow = true;
			Proj->GroundHeight = FMath::Clamp(Start.Z - GroundZ, GroundHeightMinCm, GroundHeightMaxCm);
		}
		// 서버 끝점이 먼저 와 있으면(원격 시전, 또는 타격 시점보다 빨리 온 내 시전 확정) 거기서 멈춘다
		FVector2D End;
		if (Attacker->ConsumePierceEnd(End)) Proj->SetEndPoint(End);
	}
	if (Fx)
	{
		Proj->FxComp = UGameplayStatics::SpawnEmitterAttached(Fx, Proj->GetRootComponent(), NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, /*bAutoDestroy*/ false);
	}
	return Proj;
}

void AProjectileFx::AttachMesh(UStaticMesh* Mesh, float StartScale, float EndScale, float GrowSec, float WidthCm)
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
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	MeshFrontX = Bounds.Origin.X + Bounds.BoxExtent.X;
	const float MeshWidth = Bounds.BoxExtent.Y * 2.f;
	MeshWidthMul = (WidthCm > 0.f && MeshWidth > 0.f && MeshEndScale > 0.f) ? WidthCm / (MeshWidth * MeshEndScale) : 1.f;
	ApplyMeshScale(MeshGrowSec > 0.f ? MeshStartScale : MeshEndScale);
}

void AProjectileFx::ApplyMeshScale(float LenScale)
{
	if (MeshComp == nullptr) return;
	const float Thick = LenScale * MeshWidthMul;
	MeshComp->SetRelativeScale3D(FVector(LenScale, Thick, Thick));
	// 화살 앞끝 = 액터 위치(서버 판정점). 원점이 가운데인 메시는 길이의 반(150cm)만큼 앞서 보였다(FP1 D-288)
	MeshComp->SetRelativeLocation(FVector(-MeshFrontX * LenScale, 0.f, 0.f));
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
		ApplyMeshScale(FMath::Lerp(MeshStartScale, MeshEndScale, Alpha));
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

	FVector Next = Here + Direction * Step;
	if (bGroundFollow)
	{
		float GroundZ = 0.f;
		if (TraceGround(Next, GroundZ))
			Next.Z = FMath::FInterpTo(Here.Z, GroundZ + GroundHeight, DeltaSeconds, GroundFollowInterpSpeed);
	}
	const FVector Moved = Next - Here;
	SetActorLocationAndRotation(Next, Moved.IsNearlyZero() ? Direction.Rotation() : Moved.Rotation());
	RemainDist -= Step;
	if (RemainDist <= 0.f) Finish();
}

void AProjectileFx::SetEndPoint(const FVector2D& End)
{
	// 남은 거리 = 지금 위치에서 끝점까지의 진행 방향 성분(수평). 이미 지났으면 바로 끝낸다
	const FVector Here = GetActorLocation();
	const FVector2D Dir2D = FVector2D(Direction.X, Direction.Y).GetSafeNormal();
	RemainDist = FVector2D::DotProduct(End - FVector2D(Here.X, Here.Y), Dir2D);
	if (RemainDist <= 0.f) Finish();
}

bool AProjectileFx::TraceGround(const FVector& At, float& OutZ) const
{
	UWorld* World = GetWorld();
	if (World == nullptr) return false;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectileGround), false, this);
	const FVector From = At + FVector(0.f, 0.f, GroundTraceUpCm);
	const FVector To = At - FVector(0.f, 0.f, GroundTraceDownCm);
	if (!World->LineTraceSingleByObjectType(Hit, From, To, FCollisionObjectQueryParams(ECC_WorldStatic), Params)) return false;
	OutZ = Hit.ImpactPoint.Z;
	return true;
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
