#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProjectileFx.generated.h"

class UParticleSystem;
class UParticleSystemComponent;
class UStaticMesh;
class UStaticMeshComponent;
class AJMCharacterBase;

// 투사체 스킬(HitShape Projectile)의 날아가는 모습만 그리는 클라이언트 전용 액터(D-254).
// 판정·피해는 서버가 계산하므로(SC_DAMAGE) 이 액터는 아무것도 맞히지 않는다 — 대상에 닿거나 사거리를 넘으면 사라질 뿐이다.
// 대상 지정 스킬은 매 프레임 대상의 현재 위치(몸 가운데)를 향하고, 대상이 없거나 사라지면 마지막 방향으로 곧게 날아간다.
UCLASS()
class JM_API AProjectileFx : public AActor
{
	GENERATED_BODY()

public:
	AProjectileFx();

	// Attacker의 왼손 무기 소켓(활 — 없으면 몸 가운데)에서 Fx를 붙인 연출을 만든다.
	// Target이 없으면 AimYaw 방향으로 MaxRange까지 날아간다. Speed = SkillTable.ProjSpeed(cm/s, 서버와 같은 값).
	static AProjectileFx* Launch(AJMCharacterBase* Attacker, AJMCharacterBase* Target, UParticleSystem* Fx, float AimYaw, float Speed, float MaxRange);

	// 메시를 함께 싣는다(SK2 3003 빛의 화살, D-278): 진행 방향으로 눕혀 붙이고 GrowSec 동안 StartScale → EndScale로 키운다.
	void AttachMesh(UStaticMesh* Mesh, float StartScale, float EndScale, float GrowSec);

	virtual void Tick(float DeltaSeconds) override;

private:
	// 이펙트를 끄고(리본 꼬리가 사라질 시간을 남겨) 잠시 뒤 지운다.
	void Finish();

	UPROPERTY()
	TObjectPtr<UParticleSystemComponent> FxComp;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MeshComp;
	float MeshStartScale = 1.f;
	float MeshEndScale = 1.f;
	float MeshGrowSec = 0.f;
	float Age = 0.f;

	TWeakObjectPtr<AJMCharacterBase> Target;
	FVector Direction = FVector::ForwardVector;
	float Speed = 0.f;
	float RemainDist = 0.f;
	bool bFinished = false;
};
