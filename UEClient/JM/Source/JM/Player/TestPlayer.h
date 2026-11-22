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

	// 콘솔: TestEquip <part 1~7> <itemId> — 로컬에서만 장비를 바꾼다(itemId 0 = 해제, 스탯 변화 없음)
	UFUNCTION(Exec)
	void TestEquip(int32 Part, int32 ItemId);

	// 콘솔: TestFx <이펙트 이름> — /Game/FX/<분류>/ 의 파티클을 몸 메시에 붙여 재생한다(본 이미터 확인용, 예: TestFx P_Life_Death).
	// TestFx stop = 이 명령으로 띄운 이펙트를 모두 끈다(반복 이펙트용)
	UFUNCTION(Exec)
	void TestFx(const FString& Name);

	virtual void Tick(float DeltaSeconds) override;

protected:
	// 자동 확인 스크립트(입력 없이 PIE에서 장면을 만들어 찍기 위한 것). "초:명령 인자;초:명령 인자;..." 형식.
	// 명령: attack / skill1 / equip <part> <item> / fx <이름|stop> / face <0|1> / move <앞뒤> <좌우> /
	//       cam <캐릭터 기준 yaw> <pitch> <거리> [피벗 높이] / pause
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Test")
	FString AutoScript;

	void RunAutoCommand(const FString& Command);
	FVector2D AutoMoveInput = FVector2D::ZeroVector;

	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void OnAttackInput();
	void OnSkill1Input() { OnSkillSlotInput(1); }
	// 숫자키 칸(1~5 = SkillTable Slot, SK2 D-278): 조준 단계가 있으면 조준 켜기/끄기, 없으면 바로 재생
	void OnSkillSlotInput(int32 Slot);
	void PlaySkillSlot(int32 Slot);
	int32 TestAimingSlot = 0;   // 조준 중인 칸(좌클릭이 이 칸을 확정)

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
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> Skill2Action;   // 숫자키 2(IA_QuickSlot2) — SK2 스킬2

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpringArmComponent> CameraBoom;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	// TestFx로 띄운 이펙트(stop 때 끔)
	TArray<TWeakObjectPtr<class UParticleSystemComponent>> TestFxComponents;

	float LookSensitivityYaw = 1.f;
	float LookSensitivityPitch = 1.f;
};
