#include "Player/TestPlayer.h"
#include "Character/EquipmentAppearance.h"
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
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"

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
	TestItems.Init(0, EQUIP_SLOT_COUNT);
}

void ATestPlayer::BeginPlay()
{
	Super::BeginPlay();

	// 서버 ObjectInfo 대신 테스트 값을 넣고 실제 게임과 같은 외형 경로(RefreshEquipment → UEquipmentComponent)를 탄다.
	ObjectInfo.set_object_type(Protocol::OBJECT_TYPE_PLAYER);
	ObjectInfo.set_player_type(static_cast<Protocol::PlayerType>(TestPlayerType));
	ObjectInfo.clear_equip_items();
	for (int32 i = 0; i < EQUIP_SLOT_COUNT; ++i)
		ObjectInfo.add_equip_items(TestItems.IsValidIndex(i) ? TestItems[i] : 0);
	// 최고 속도는 BP의 MaxWalkSpeed(테스트 전용). 조준 배율(SkillTable.AimMoveSpeedMul)이 이 값에 곱해지도록 스탯에 넣는다.
	ObjectInfo.mutable_stat()->set_move_speed(GetCharacterMovement()->MaxWalkSpeed);
	RefreshEquipment();

	// 자동 확인 스크립트: 항목마다 타이머를 건다
	// BP 값이 비어 있으면 Saved/TestAuto.txt를 읽는다(에셋을 건드리지 않고 도구가 장면을 바꿔 가며 찍을 수 있게)
	FString Script = AutoScript;
	if (Script.IsEmpty())
		FFileHelper::LoadFileToString(Script, *(FPaths::ProjectSavedDir() / TEXT("TestAuto.txt")));
	Script.ReplaceInline(TEXT("\r"), TEXT(""));
	Script.ReplaceInline(TEXT("\n"), TEXT(";"));
	TArray<FString> Entries;
	Script.ParseIntoArray(Entries, TEXT(";"), true);
	for (const FString& Entry : Entries)
	{
		FString TimeText, Command;
		if (!Entry.Split(TEXT(":"), &TimeText, &Command)) continue;
		const float Delay = FMath::Max(FCString::Atof(*TimeText), 0.01f);
		Command.TrimStartAndEndInline();
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Command]() { RunAutoCommand(Command); }), Delay, false);
	}
}

void ATestPlayer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!AutoMoveInput.IsZero() && GetController())
	{
		const FRotator YawRotation(0, GetController()->GetControlRotation().Yaw, 0);
		AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), AutoMoveInput.X);
		AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), AutoMoveInput.Y);
	}
}

void ATestPlayer::RunAutoCommand(const FString& Command)
{
	TArray<FString> Args;
	Command.ParseIntoArrayWS(Args);
	if (Args.Num() == 0) return;
	const FString& Verb = Args[0];
	auto Num = [&Args](int32 Index, float Default) { return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : Default; };

	UE_LOG(LogTemp, Log, TEXT("TestPlayer auto: %s"), *Command);
	if (Verb == TEXT("attack")) OnAttackInput();
	else if (Verb == TEXT("skill1")) OnSkill1Input();
	else if (Verb == TEXT("equip")) TestEquip(static_cast<int32>(Num(1, 0)), static_cast<int32>(Num(2, 0)));
	else if (Verb == TEXT("fx") && Args.IsValidIndex(1)) TestFx(Args[1]);
	else if (Verb == TEXT("face")) bFaceCameraOnAttack = Num(1, 1) != 0.f;
	else if (Verb == TEXT("move")) AutoMoveInput = FVector2D(Num(1, 0), Num(2, 0));
	else if (Verb == TEXT("pause")) UGameplayStatics::SetGamePaused(this, true);
	else if (Verb == TEXT("cam"))
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
			PC->SetControlRotation(FRotator(Num(2, -10), GetActorRotation().Yaw + Num(1, 0), 0.f));
		CameraBoom->TargetArmLength = Num(3, 450);
		CameraBoom->SocketOffset = FVector::ZeroVector;
		if (Args.IsValidIndex(4)) CameraBoom->TargetOffset = FVector(0.f, 0.f, Num(4, 0));
	}
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
	// 조준 중이면 조준한 스킬을 확정한다(실제 게임과 같이 시전 방향 고정 뒤 조준 해제)
	if (IsSkillAiming())
	{
		PlaySkillSlot(1);
		SetSkillAiming(0);
		return;
	}
	PlaySkillSlot(0);
}

void ATestPlayer::OnSkill1Input()
{
	// 조준 단계가 있는 스킬(AimMaxMs > 0)은 조준 켜기/끄기(로컬 — 감속·카메라 방향·조준 자세 확인용), 없으면 바로 재생한다.
	if (IsSkillAiming())
	{
		SetSkillAiming(0);
		return;
	}
	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FSkillTableRow* Skill = DataManager ? DataManager->FindSkillBySlot(GetWeaponType(), 1) : nullptr;
	const int32 SkillId = DataManager ? DataManager->FindSkillIdBySlot(GetWeaponType(), 1) : 0;
	if (Skill && Skill->AimMaxMs > 0 && SkillId != 0)
	{
		SetSkillAiming(SkillId);
		return;
	}
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
	if (Index < 0 || Index >= EQUIP_SLOT_COUNT) return;

	while (ObjectInfo.equip_items_size() < EQUIP_SLOT_COUNT) ObjectInfo.add_equip_items(0);
	ObjectInfo.set_equip_items(Index, ItemId);
	if (Equipment) Equipment->SetSlotItem(Index, ItemId);

	// 서버와 같은 규칙(MC4.2f): 무기가 한손검이 아니게 되면 보조손(방패)을 비운다
	const int32 OffhandIndex = static_cast<int32>(Protocol::EQUIP_PART_OFFHAND) - 1;
	if (Part == static_cast<int32>(Protocol::EQUIP_PART_WEAPON) && GetWeaponType() != EWeaponType::Sword && ObjectInfo.equip_items(OffhandIndex) != 0)
	{
		ObjectInfo.set_equip_items(OffhandIndex, 0);
		if (Equipment) Equipment->SetSlotItem(OffhandIndex, 0);
	}
}

void ATestPlayer::TestFx(const FString& Name)
{
	if (Name.Equals(TEXT("stop"), ESearchCase::IgnoreCase))
	{
		for (const TWeakObjectPtr<UParticleSystemComponent>& Comp : TestFxComponents)
			if (Comp.IsValid()) Comp->DestroyComponent();
		TestFxComponents.Reset();
		return;
	}

	static const TCHAR* Categories[] = { TEXT("Life"), TEXT("Buff"), TEXT("Cast"), TEXT("Hit"), TEXT("AoE"), TEXT("Trail"), TEXT("Proj") };
	UParticleSystem* Template = nullptr;
	for (const TCHAR* Category : Categories)
	{
		const FString PackagePath = FString::Printf(TEXT("/Game/FX/%s/%s"), Category, *Name);
		if (!FPackageName::DoesPackageExist(PackagePath)) continue;
		Template = LoadObject<UParticleSystem>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *Name));
		if (Template) break;
	}
	if (Template == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("TestFx: '%s' not found under /Game/FX/<Category>/"), *Name);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, FString::Printf(TEXT("TestFx: %s not found"), *Name));
		return;
	}

	// Attach to the body mesh so bone/socket emitters read the mannequin bones (attach-parent fallback).
	UParticleSystemComponent* Comp = UGameplayStatics::SpawnEmitterAttached(Template, GetMesh(), NAME_None,
		FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true);
	if (Comp) TestFxComponents.Add(Comp);
	UE_LOG(LogTemp, Log, TEXT("TestFx: %s %s"), *Name, Comp ? TEXT("spawned") : TEXT("spawn failed"));
}
