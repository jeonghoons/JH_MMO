#pragma once

#include "CoreMinimal.h"
#include "Player/JMPlayer.h"
#include "JMMyPlayer.generated.h"

struct FInputActionValue;
class USpringArmComponent;
class UCameraComponent;
class UInputAction;
struct FSkillTableRow;

UCLASS()
class JM_API AJMMyPlayer : public AJMPlayer
{
	GENERATED_BODY()
public:
	AJMMyPlayer();

protected:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaTime) override;
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void OnAttackInput();
	void OnSkill1Input();
	void OnCancelAimInput();

public:
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }

public:
	UFUNCTION(BlueprintCallable, Category = "Action")
	void RequestAttack();

	// 서버가 내 이동을 거부했을 때(SC_MOVE 보정) 서버 위치로 돌아가고, 이후 이동 패킷에 그 순번을 싣는다. (서버 권위 이동, S1)
	void ApplyServerCorrection(const Protocol::PositionInfo& ServerPos, uint32 Seq);

	// 스킬 조준(MC4.2d-1, DECISIONS 2026-11-09): 내 조준 상태는 로컬이 권위다. 서버가 본인에게 보낸 해제(거부·시간 초과)는
	// Seq가 지금 조준의 것일 때만 반영한다(늦게 온 옛 응답이 새 조준을 끄지 않게).
	void OnServerAimOff(uint32 Seq);
	// 내 무기가 바뀌면 조준을 끈다. 서버에도 끄기를 보낸다(서버가 이미 해제했으면 무시).
	void CancelLocalAim();
	// 서버가 적용한 스킬 쿨타임(SC_ATTACK.cooldown_ms)을 기록한다. 쿨타임 중에는 조준에 들어가지 않는다.
	void NoteSkillCooldown(int32 SkillId, int32 CooldownMs);
	virtual void OnDead() override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	class UInputMappingContext* DefaultMappingContext;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MoveAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* LookAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MouseLookAction;

	// 전투 입력 (A2.2c): 좌클릭 = 일반공격/조준 스킬 확정, 1번 키 = 조준 진입, 우클릭 = 조준 취소
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* AttackAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* Skill1Action;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* CancelAimAction;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

protected:
	// 이동 패킷 전송(S1): 움직이는 동안만 MoveSendInterval마다 보내고, 입력이 바뀌거나 멈추면 한 번 더 보낸다.
	// 서버가 MoveMinInterval보다 잦은 패킷을 버리므로 그 안에 생긴 전송은 미뤘다가 보낸다. 수치는 MovementTable(PossessedBy)
	float MoveSendInterval = 0.2f;
	float MoveMinInterval = 0.05f;
	float MovePacketSendTimer = 0.f;
	float TimeSinceMoveSend = 1.f;
	bool bMoveSendPending = false;
	bool bWasMoving = false;
	// 속도 방향 변화로 보내기(MC4.1b): 마지막으로 보낸 속도 방향, 기준 각도의 cos(MoveSendTurnDeg 0이면 끔)
	FVector2D LastSentVelDir = FVector2D::ZeroVector;
	float MoveSendTurnCos = -2.f;
	uint32 CorrectionSeq = 0;

	FVector2D	DesiredInput;
	FVector		DesiredMovementDir;
	float		DesiredYaw;

	FVector2D	LastDesiredInput;

	// MovementTable에서 읽은 마우스 감도 배율 (PossessedBy에서 설정)
	float LookSensitivityYaw = 1.f;
	float LookSensitivityPitch = 1.f;

private:
	// 크로스헤어(화면 중앙) 기준 조준 결과
	struct FAimResult
	{
		bool bValid = false;
		float Yaw = 0.f;                 // 시전 방향(deg)
		int32 TargetId = 0;              // Targeted: 서버 오브젝트 id
		FVector2D Point = FVector2D::ZeroVector; // 지면 지정 스킬의 목표 지점
	};

	const FSkillTableRow* FindMySkill(int32 Slot) const;
	bool ComputeAim(const FSkillTableRow& Skill, FAimResult& Out) const;
	void DrawAimIndicator(const FSkillTableRow& Skill, const FAimResult& Aim) const;
	void CastSkillSlot(int32 Slot);

	// 조준 켜기/끄기. 켜기는 쿨타임·사망·빈도·AimMaxMs를 로컬에서 먼저 본다. bSendOff = 서버에 끄기를 보낼지(확정 공격은 서버가 해제하므로 false)
	void BeginLocalAim(int32 Slot);
	void EndLocalAim(bool bSendOff);

	bool bAiming = false;
	int32 AimingSlot = 0;
	uint32 AimSeq = 0;                 // CS_SKILL_AIM을 보낼 때마다 +1
	double AimStartTime = 0.0;         // FPlatformTime::Seconds
	double LastAimOnTime = -1.0e9;
	int32 AimMaxMs = 0;                // 지금 조준 스킬의 최대 유지 시간. 지나면 로컬에서 취소를 보낸다
	float AimMinInterval = 0.2f;       // MovementTable.ServerAimMinIntervalMs (PossessedBy)
	TMap<int32, double> SkillCooldownEnd;   // 스킬 ID → 쿨타임 끝 시각(FPlatformTime::Seconds)
	uint64 LastAttackInputFrame = 0;
};
