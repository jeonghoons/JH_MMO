#pragma once

#include "CoreMinimal.h"
#include "Player/JMPlayer.h"
#include "TestPlayer.generated.h"

struct FInputActionValue;
class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UAnimMontage;

// 서버 없는 테스트 레벨(/Game/Maps/TestLevel) 전용 플레이어(DECISIONS 2026-11-09 "테스트 레벨").
// 외형·애니메이션은 실제 게임과 같은 경로(UEquipmentComponent → BodyTable·ItemTable·WeaponTypeTable, ABP_PlayerBase)를 쓰고,
// 패킷을 보내지 않으며 공격은 로컬에서 몽타주만 재생한다. 판정·스탯은 없다(서버 권위 기능은 실제 게임에서 확인).
// BP_TestPlayer(/Game/_Core/Blueprints/Test)가 입력 에셋과 기본 장비를 채운다(jm_kit.testbed).
UCLASS()
class JM_API ATestPlayer : public AJMPlayer
{
	GENERATED_BODY()

public:
	ATestPlayer();

	// 콘솔: TestEquip <part 1~6> <itemId> — 로컬에서만 장비를 바꾼다(itemId 0 = 해제, 스탯 변화 없음)
	UFUNCTION(Exec)
	void TestEquip(int32 Part, int32 ItemId);

protected:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void OnAttackInput();
	void OnSkill1Input();
	void PlaySkillSlot(int32 Slot);

	// 체형(1 MALE, 2 FEMALE)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test")
	int32 TestPlayerType = 1;

	// 시작 장비(인덱스 = EquipPart - 1, 빈 슬롯 0). ItemTable ID
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test")
	TArray<int32> TestItems;

	// 지정하면 좌클릭(슬롯 0)에 SkillTable 몽타주 대신 이 몽타주를 재생한다(새 몽타주를 테이블 변경 없이 확인)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test")
	TObjectPtr<UAnimMontage> AttackMontageOverride;

	// 공격할 때 카메라 방향으로 돌리고 타격 시점까지 방향을 고정한다(실제 게임의 대상 방향 고정을 흉내)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test")
	bool bFaceCameraOnAttack = true;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> LookAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> MouseLookAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> AttackAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> Skill1Action;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpringArmComponent> CameraBoom;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	float LookSensitivityYaw = 1.f;
	float LookSensitivityPitch = 1.f;
};
