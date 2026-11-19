#pragma once

#include "CoreMinimal.h"
#include "Player/JMPlayer.h"
#include "Player/QuickSlot.h"
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
	// 숫자키 N(Index = N-1): 칸 내용이 스킬이면 조준 진입(조준 중에 같은 칸이면 취소), 빈 칸은 무시
	void OnQuickSlotInput(int32 Index);
	void OnCancelAimInput();
	void OnInventoryInput();
	// F 키(A6.2): 줍기 거리 안의 가장 가까운 바닥 드랍을 줍겠다고 요청한다. 결과는 서버 패킷으로 온다.
	void OnInteractInput();
	// T 키(A6.3a): 크로스헤어에 가장 가까운 플레이어에게 1:1 거래를 요청한다. 거리·상태 검증은 서버가 한다.
	void OnTradeInput();
	// 줍기 안내(HUD)를 지금 가까운 드랍에 맞춘다. 대상이 바뀔 때만 HUD를 고친다.
	void UpdateInteractPrompt();

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

	// 선반영(CP1, D-211·D-212): 내 스킬·공격은 CastSkillSlot이 입력 즉시 몽타주·이펙트·쿨타임을 시작하고 요청 순번을 붙여 보낸다.
	// ConfirmCast = 내 SC_ATTACK. true면 먼저 시작한 시전이므로 다시 재생하지 않는다. false = 순번 0이거나 서버가 다른 스킬로 확정(먼저 시작한 것은 되돌림).
	bool ConfirmCast(uint32 Seq, int32 SkillId, float AimX, float AimY);
	// RollbackCast = SC_ATTACK_RESULT(거부). 순번이 응답을 기다리는 시전과 같을 때만 몽타주·이펙트·쿨타임·잠금을 되돌린다.
	// Result = Protocol::AttackResult, RemainMs = ACTING·COOLDOWN일 때 서버 기준 남은 시간(로컬 시각을 거기에 맞춘다).
	void RollbackCast(uint32 Seq, int32 Result, int32 RemainMs);
	// 개발용(콘솔 AutoAttack·AttackStats): 시전할 수 있게 되는 첫 프레임마다 Slot을 Count번 시전한다 / 보낸·확정·거부 수를 글로 돌려준다.
	void DevAutoAttack(int32 Count, int32 Slot);
	FString DevAttackStats(bool bReset);
	// 내 부활(SC_REVIVE): 서버 위치로 옮기고 이후 이동에 실을 보정 순번을 받는다.
	void OnMyRevive(const Protocol::PositionInfo& Pos, int32 Hp, uint32 Seq);
	// 내 순간이동(SC_TELEPORT, L5 포털): 서버 위치로 옮기고 이후 이동에 실을 보정 순번을 받는다.
	void OnMyTeleport(const Protocol::PositionInfo& Pos, uint32 Seq);

	// 액티브 칸(D-148): 칸 수 = QuickSlotActions 수. 지금은 규칙 배치 — 칸 N = 현재 무기의 SkillTable 슬롯 N(없으면 빈 칸).
	// 무기가 바뀌면 다시 부른다(UJMObjectManager가 스폰·장비 변경 때 부르고 HUD에 넘긴다).
	void RebuildQuickSlots();
	const TArray<FQuickSlot>& GetQuickSlots() const { return QuickSlots; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	class UInputMappingContext* DefaultMappingContext;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MoveAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* LookAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MouseLookAction;

	// 전투 입력 (A2.2c): 좌클릭 = 일반공격/조준 스킬 확정, 숫자키 = 액티브 칸(스킬이면 조준 진입), 우클릭 = 조준 취소
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* AttackAction;
	// 액티브 칸 입력(D-148). 인덱스 0 = 숫자키 1(IA_Skill1), 1~4 = IA_QuickSlot2~5. 칸 수도 이 배열 길이로 정한다.
	UPROPERTY(EditAnywhere, Category = "Input")
	TArray<TObjectPtr<UInputAction>> QuickSlotActions;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* CancelAimAction;
	// 인벤토리·장비창 여닫기(I 키, A6.1)
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> InventoryAction;
	// 상호작용(F 키, A6.2): 지금은 바닥 드랍 줍기
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;
	// 거래 요청(T 키, A6.3a)
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> TradeAction;

	// 줍기 안내 대상(가장 가까운 드랍)과 다음 검사까지 남은 시간
	TWeakObjectPtr<class ADropItemActor> InteractTarget;
	float InteractCheckTimer = 0.f;
	bool bInteractPromptShown = false;
	// 안내 중인 상점 NPC의 오브젝트 id(0 = 없음). 드랍과 NPC 중 가까운 쪽 하나만 안내한다 (L4)
	int32 InteractNpcId = 0;
	// 안내 중인 포털 id(0 = 없음). 드랍·NPC가 없을 때만 안내한다 (L5)
	int32 InteractPortalId = 0;

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
	// 스킬 조준 중 제자리에서 카메라로 몸을 돌리면 방향만 바뀌어도 보낸다(MC4.2d-2 — 원격도 같은 방향을 봄). 기준 = MoveSendTurnDeg
	float MoveSendTurnDegValue = 0.f;
	float LastSentYaw = 0.f;
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
	// Duration < 0이면 한 프레임(조준 중 매 틱 다시 그림), 0 이상이면 그 시간 동안 남긴다(시전한 공격의 범위 표시, D-199).
	void DrawAimIndicator(const FSkillTableRow& Skill, const FAimResult& Aim, float Duration = -1.f) const;
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

	// 이동 잠금(D-199): 시전부터 휘두르기 끝(SkillTable.MoveLockMs)까지 이동 입력을 받지 않는다. 서버도 같은 시간 동안 이동을 거부한다.
	// LocalActionUntil = 내가 보낸 시전의 동작이 끝나는 시각(월드 초). 그 전에 누른 공격은 서버가 받지 않으므로 잠금·범위 표시를 다시 걸지 않는다.
	double MoveLockUntil = 0.0;
	double LocalActionUntil = 0.0;
	bool IsMoveLocked() const;

	// 응답을 기다리는 내 시전(CP1). 한 번에 하나: 응답 전에는 다음 시전을 보내지 않는다(PendingCastTimeoutSec이 지나면 풀림).
	struct FPendingCast
	{
		bool bActive = false;
		uint32 Seq = 0;
		int32 SkillId = 0;
		int32 Slot = 0;
		double StartTime = 0.0;            // 월드 초
		double PrevCooldownEnd = 0.0;      // 시전 전의 SkillCooldownEnd 값(없었으면 0)
		double PrevLocalActionUntil = 0.0;
		double PrevMoveLockUntil = 0.0;
		TWeakObjectPtr<class UAnimMontage> Montage;                  // 먼저 재생한 몽타주
		TWeakObjectPtr<class UParticleSystemComponent> FxComp;       // 먼저 만든 장판 이펙트
		FTimerHandle FxTimer;                                        // 장판 끄기 또는 타격 시점 이펙트 대기
	};
	FPendingCast PendingCast;
	uint32 AttackSeq = 0;              // CS_ATTACK을 보낼 때마다 +1
	// 지금 로컬 기준으로 시전을 보낼 수 있는가: 생존·앞 동작 끝·쿨타임·응답 대기 없음. 서버만 아는 것(피격 경직·무기 교체 잠금)은 거부로 온다.
	bool CanCastLocally(int32 SkillId);
	// 먼저 시작한 것을 되돌린다(몽타주·이펙트·쿨타임 표시·이동 잠금·방향 고정·범위 표시).
	void UndoPendingCast();
	class UCombatHudWidget* GetCombatHud() const;

	// 개발용 통계·자동 공격
	int32 DevAutoRemain = 0;
	int32 DevAutoSlot = 0;
	int32 StatCastSent = 0, StatCastConfirmed = 0, StatCastRejected = 0, StatCastTimeout = 0;
	int32 StatCastRejectedBy[16] = {};

	TArray<FQuickSlot> QuickSlots;
};
