#include "Player/TestPlayer.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Animation/AnimMontage.h"
#include "Character/EquipmentComponent.h"
#include "Game/DataManager.h"
#include "Data/SkillTableRow.h"

ATestPlayer::ATestPlayer()
{
	// 카메라 구성은 AJMMyPlayer와 같다. 수치는 PossessedBy에서 MovementTable 1001로 덮어쓴다.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 450.0f;
	CameraBoom->TargetOffset = FVector(0.f, 0.f, 150.f);
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;

	// UJMAnimInstance가 내 캐릭터로 보고 실제 속도·가속도로 이동 애니메이션을 고른다. 원격 보간(ApplyNetworkMovement)도 꺼진다.
	IsMyPlayer = true;
	TestItems.Init(0, 6);
}

void ATestPlayer::BeginPlay()
{
	Super::BeginPlay();

	// 서버 ObjectInfo 대신 테스트 값을 넣고 실제 게임과 같은 외형 경로(RefreshEquipment → UEquipmentComponent)를 탄다.
	ObjectInfo.set_object_type(Protocol::OBJECT_TYPE_PLAYER);
	ObjectInfo.set_player_type(static_cast<Protocol::PlayerType>(TestPlayerType));
	ObjectInfo.clear_equip_items();
	for (int32 i = 0; i < 6; ++i)
		ObjectInfo.add_equip_items(TestItems.IsValidIndex(i) ? TestItems[i] : 0);
	RefreshEquipment();
}

void ATestPlayer::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// 이동/카메라 수치는 AJMMyPlayer::PossessedBy와 같은 MovementTable 1001 열을 쓴다. 최고 속도는 서버 스탯이라 BP의 MaxWalkSpeed(테스트 전용) 그대로.
	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FMovementTableRow* Row = DataManager ? DataManager->FindMovementRow(1001) : nullptr;
	if (Row == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("TestPlayer: MovementTable row 1001 not found; using built-in camera defaults."));
		return;
	}

	CameraBoom->TargetArmLength = Row->CameraArmLengthCm;
	CameraBoom->TargetOffset = FVector(0.f, 0.f, Row->CameraPivotHeightCm);
	CameraBoom->SocketOffset = FVector(0.f, Row->CameraShoulderOffsetCm, 0.f);
	LookSensitivityYaw = Row->LookSensitivityYaw;
	LookSensitivityPitch = Row->LookSensitivityPitch;

	GetCharacterMovement()->RotationRate = FRotator(0.f, Row->RotationRateYawDegSec, 0.f);
	GetCharacterMovement()->MaxAcceleration = Row->AccelCmSec2;
	GetCharacterMovement()->BrakingDecelerationWalking = Row->BrakeDecelCmSec2;

	if (APlayerController* PC = Cast<APlayerController>(NewController))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->ViewPitchMin = Row->CameraMinPitchDeg;
			PC->PlayerCameraManager->ViewPitchMax = Row->CameraMaxPitchDeg;
		}
		const float DefaultPitch = Row->CameraDefaultPitchDeg;
		PC->SetControlRotation(FRotator(DefaultPitch, GetActorRotation().Yaw, 0.f));
		// 엔진의 ClientRestart가 같은 프레임에 컨트롤 회전을 덮어쓰므로 다음 틱에 한 번 더 적용한다(AJMMyPlayer와 같음).
		GetWorldTimerManager().SetTimerForNextTick([this, DefaultPitch]()
		{
			if (APlayerController* P = Cast<APlayerController>(GetController()))
				P->SetControlRotation(FRotator(DefaultPitch, GetActorRotation().Yaw, 0.f));
		});
	}
}

void ATestPlayer::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 테스트 GameMode는 기본 PlayerController를 쓰므로 AJMPlayerController로 캐스트하지 않는다.
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->ClearAllMappings();
			if (DefaultMappingContext) Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}

	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ATestPlayer::Move);
			Input->BindAction(MoveAction, ETriggerEvent::Completed, this, &ATestPlayer::Move);
		}
		if (MouseLookAction) Input->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ATestPlayer::Look);
		if (LookAction) Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ATestPlayer::Look);
		if (AttackAction) Input->BindAction(AttackAction, ETriggerEvent::Started, this, &ATestPlayer::OnAttackInput);
		if (Skill1Action) Input->BindAction(Skill1Action, ETriggerEvent::Started, this, &ATestPlayer::OnSkill1Input);
	}
}

void ATestPlayer::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();
	if (GetController() == nullptr) return;

	// 카메라(컨트롤러) 방향 기준 이동 — AJMMyPlayer::Move와 같다.
	const FRotator YawRotation(0, GetController()->GetControlRotation().Yaw, 0);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), MovementVector.Y);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), MovementVector.X);
}

void ATestPlayer::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();
	if (GetController() == nullptr) return;

	AddControllerYawInput(LookAxisVector.X * LookSensitivityYaw);
	AddControllerPitchInput(LookAxisVector.Y * LookSensitivityPitch);
}

void ATestPlayer::OnAttackInput()
{
	PlaySkillSlot(0);
}

void ATestPlayer::OnSkill1Input()
{
	// 조준 단계 없이 바로 재생한다(조준 동기화는 MC4.2d, 서버가 필요해 실제 게임에서 확인).
	PlaySkillSlot(1);
}

void ATestPlayer::PlaySkillSlot(int32 Slot)
{
	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FSkillTableRow* Skill = DataManager ? DataManager->FindSkillBySlot(GetWeaponType(), Slot) : nullptr;

	if (bFaceCameraOnAttack && GetController())
	{
		FaceYaw(GetController()->GetControlRotation().Yaw);
		if (Skill) LockFacingForSkill(*Skill);
	}

	if (Slot == 0 && AttackMontageOverride)
	{
		if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
			AnimInst->Montage_Play(AttackMontageOverride, GetAttackPlayRate(Skill));
		return;
	}

	if (Skill == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("TestPlayer: no skill for weapon type %d slot %d"), static_cast<int32>(GetWeaponType()), Slot);
		return;
	}
	OnAttack(Skill);
}

void ATestPlayer::TestEquip(int32 Part, int32 ItemId)
{
	const int32 Index = Part - 1;
	if (Index < 0 || Index >= 6) return;

	while (ObjectInfo.equip_items_size() < 6) ObjectInfo.add_equip_items(0);
	ObjectInfo.set_equip_items(Index, ItemId);
	if (Equipment) Equipment->SetSlotItem(Index, ItemId);
}
