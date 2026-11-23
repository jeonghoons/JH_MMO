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
	static AProjectileFx* Launch(AJMCharacterBase* Attacker, AJMCharacterBase* Target, UParticleSystem* Fx, float AimYaw, float Speed, float MaxRange, float StartFwdCm = 0.f);

	// 메시를 함께 싣는다(SK2 3003 빛의 화살, D-278): 진행 방향으로 눕혀 붙이고 GrowSec 동안 StartScale → EndScale로 키운다.
	// WidthCm > 0이면 굵기(Y·Z)를 판정 폭에 맞추고(길이와 따로), 메시 앞끝(+X)이 액터 위치(= 판정점)에 오게 뒤로 붙인다(FP1 D-288).
	void AttachMesh(UStaticMesh* Mesh, float StartScale, float EndScale, float GrowSec, float WidthCm = 0.f);

	// 서버가 알려 준 끝점(관통 투사체가 벽·지형에 막혀 멈추는 곳, SC_ATTACK aim 지점)에서 멈추게 남은 거리를 고친다(FP1).
	void SetEndPoint(const FVector2D& End);

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
	float MeshWidthMul = 1.f;     // 굵기 배율 ÷ 길이 배율(판정 폭 ÷ (메시 폭 × 최종 배율))
	float MeshFrontX = 0.f;       // 메시 로컬 앞끝 X(배율 1)
	void ApplyMeshScale(float LenScale);
	float Age = 0.f;

	TWeakObjectPtr<AJMCharacterBase> Target;
	FVector Direction = FVector::ForwardVector;
	float Speed = 0.f;
	float RemainDist = 0.f;
	bool bFinished = false;
	// 대상 없는 관통 투사체: 수평으로 나아가며 높이는 지면 + GroundHeight를 따라간다(FP1 — 비탈에서 땅에 묻히거나 떠 보이던 것)
	bool bGroundFollow = false;
	float GroundHeight = 0.f;
	// 지면 높이(못 찾으면 false). 캐릭터·투사체가 아닌 고정 지형·충돌 도형만 본다
	bool TraceGround(const FVector& At, float& OutZ) const;
};
