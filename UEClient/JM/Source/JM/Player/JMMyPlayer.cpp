#include "Player/JMMyPlayer.h"
#include "Network/NetMoveStats.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "JMPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Network/NetworkManager.h"
#include "Protocol/Protocol.pb.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Game/DataManager.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Data/SkillTableRow.h"
#include "Character/NpcCharacter.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"
#include "UI/CombatHudWidget.h"
#include "Game/DropItemActor.h"
#include "Game/JMInventoryManager.h"
#include "Game/JMObjectManager.h"
#include "Game/JMTradeManager.h"
#include "Game/JMShopManager.h"
#include "Game/JMPartyManager.h"
#include "Game/JMAuctionManager.h"
#include "Game/JMQuestManager.h"
#include "UI/PlayerMenuWidget.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Particles/ParticleSystemComponent.h"
#include "TimerManager.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// 응답(SC_ATTACK·SC_ATTACK_RESULT)이 이 시간 안에 오지 않으면 대기를 풀어 다음 시전을 보낼 수 있게 한다(되돌리지는 않음).
	constexpr double PendingCastTimeoutSec = 2.0;
}

AJMMyPlayer::AJMMyPlayer()
{
	// 3인칭 마우스 회전 카메라(검은사막 방식): 마우스로 카메라가 캐릭터 주위를 돈다.
	// 수치는 PossessedBy에서 MovementTable 값으로 덮어쓴다. 여기 값은 테이블이 없을 때의 대비용이다.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 450.0f;
	CameraBoom->TargetOffset = FVector(0.f, 0.f, 150.f);   // 캐릭터를 화면 아래쪽에, 크로스헤어를 중앙에 두기 위해 피벗을 위로 올린다
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// 이동 방향을 바라본다. 공격/조준할 때는 카메라 방향을 보게 한다. (A2.2c)
	GetCharacterMovement()->bOrientRotationToMovement = true;

	IsMyPlayer = true;

	// 바닥 범위 표시(SK2, D-278): 월드 위치·회전으로 둔다(캐릭터 회전을 따라가지 않음). 아래로 투영(pitch -90).
	SkillIndicator = CreateDefaultSubobject<UDecalComponent>(TEXT("SkillIndicator"));
	SkillIndicator->SetupAttachment(RootComponent);
	SkillIndicator->SetUsingAbsoluteLocation(true);
	SkillIndicator->SetUsingAbsoluteRotation(true);
	SkillIndicator->SetUsingAbsoluteScale(true);
	SkillIndicator->SetWorldRotation(FRotator(-90.f, 0.f, 0.f));
	SkillIndicator->SetVisibility(false);
	SkillIndicatorMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/FX/Indicator/M_SkillIndicator.M_SkillIndicator")));
}

void AJMMyPlayer::BeginPlay()
{
	Super::BeginPlay();

	// 몸은 이동 방향을 보고, 카메라 방향은 조준 중에만 본다(ApplyAimRotationMode). BP_MyPlayer 기본값에
	// 컨트롤러 회전(desired·pitch·roll)이 켜져 있어 공격의 방향 고정 동안 몸이 마우스를 따라 돌고 위아래로 기울었다(FP1).
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	if (UCharacterMovementComponent* Move = GetCharacterMovement()) Move->bUseControllerDesiredRotation = false;
}

void AJMMyPlayer::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// 이동/카메라 수치는 코드에 두지 않고 MovementTable(ID 1001 = 플레이어 기본값)에서 읽는다.
	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FMovementTableRow* Row = DataManager ? DataManager->FindMovementRow(1001) : nullptr;
	if (Row == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("MyPlayer: MovementTable row 1001 not found; using built-in camera defaults."));
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

	MoveSendInterval = Row->MoveSendIntervalMs / 1000.f;
	MoveMinInterval = Row->ServerMinMoveIntervalMs / 1000.f;
	MoveSendTurnCos = Row->MoveSendTurnDeg > 0.f ? FMath::Cos(FMath::DegreesToRadians(Row->MoveSendTurnDeg)) : -2.f;
	AimMinInterval = Row->ServerAimMinIntervalMs / 1000.f;
	MoveSendTurnDegValue = Row->MoveSendTurnDeg;
	AutoWalkTimeoutSec = Row->AutoWalkTimeoutMs / 1000.f;
	AutoWalkStuckSec = Row->AutoWalkStuckMs / 1000.f;
	PickMaxDistCm = Row->PickMaxDistCm;
	PickScreenRadiusPx = Row->PickScreenRadiusPx;

	// 카메라 피치 제한과 시작 각도 (음수 피치 = 아래로 내려다봄)
	if (APlayerController* PC = Cast<APlayerController>(NewController))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->ViewPitchMin = Row->CameraMinPitchDeg;
			PC->PlayerCameraManager->ViewPitchMax = Row->CameraMaxPitchDeg;
		}
		PC->SetControlRotation(FRotator(Row->CameraDefaultPitchDeg, GetActorRotation().Yaw, 0.f));

		// 엔진의 ClientRestart가 같은 프레임에 컨트롤 회전을 액터 방향(피치 0)으로 덮어쓰므로 다음 틱에 한 번 더 적용한다.
		const float DefaultPitch = Row->CameraDefaultPitchDeg;
		GetWorldTimerManager().SetTimerForNextTick([this, DefaultPitch]()
		{
			if (APlayerController* P = Cast<APlayerController>(GetController()))
				P->SetControlRotation(FRotator(DefaultPitch, GetActorRotation().Yaw, 0.f));
		});
	}
}


void AJMMyPlayer::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (AJMPlayerController* PC = Cast<AJMPlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->ClearAllMappings();
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
				UE_LOG(LogTemp, Log, TEXT("PossessedBy: Success InputMapping!"));
			}
		}

	}


	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AJMMyPlayer::Move);
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &AJMMyPlayer::Move);

		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AJMMyPlayer::Look);
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AJMMyPlayer::Look);
		if (AttackAction) EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Started, this, &AJMMyPlayer::OnAttackInput);
		for (int32 Index = 0; Index < QuickSlotActions.Num(); ++Index)
		{
			if (QuickSlotActions[Index]) EnhancedInputComponent->BindAction(QuickSlotActions[Index].Get(), ETriggerEvent::Started, this, &AJMMyPlayer::OnQuickSlotInput, Index);
		}
		// 우클릭(IA_CancelAim): 조준 취소 + 커서 모드 상호작용(UI6)
		if (CancelAimAction) EnhancedInputComponent->BindAction(CancelAimAction, ETriggerEvent::Started, this, &AJMMyPlayer::OnRightClickInput);
		if (InventoryAction) EnhancedInputComponent->BindAction(InventoryAction.Get(), ETriggerEvent::Started, this, &AJMMyPlayer::OnInventoryInput);
		if (InteractAction) EnhancedInputComponent->BindAction(InteractAction.Get(), ETriggerEvent::Started, this, &AJMMyPlayer::OnInteractInput);
		if (PartyAction) EnhancedInputComponent->BindAction(PartyAction.Get(), ETriggerEvent::Started, this, &AJMMyPlayer::OnPartyInput);
		if (AuctionAction) EnhancedInputComponent->BindAction(AuctionAction.Get(), ETriggerEvent::Started, this, &AJMMyPlayer::OnAuctionInput);
		if (QuestAction) EnhancedInputComponent->BindAction(QuestAction.Get(), ETriggerEvent::Started, this, &AJMMyPlayer::OnQuestInput);
		
	}
}

void AJMMyPlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 줍기 안내는 0.1초마다 맞춘다(드랍 수만큼 거리 비교).
	InteractCheckTimer -= DeltaTime;
	if (InteractCheckTimer <= 0.f)
	{
		InteractCheckTimer = 0.1f;
		UpdateInteractPrompt();
	}

	// 커서 모양(UI6): 커서가 보이는 동안 0.1초마다 마우스 아래 대상을 본다.
	CursorPickTimer -= DeltaTime;
	if (CursorPickTimer <= 0.f)
	{
		CursorPickTimer = 0.1f;
		UpdateHoverCursor();
	}

	// 직선 자동 걷기(UI6): 이동 입력을 대신 넣는다. 서버 이동 검증은 방향키 이동과 같은 경로다.
	if (IsAutoWalking()) TickAutoWalk();

	if (bAiming)
	{
		const FSkillTableRow* AimSkill = FindMySkill(AimingSlot);
		FAimResult Aim;
		if (AimSkill == nullptr) EndLocalAim(true);
		else if ((FPlatformTime::Seconds() - AimStartTime) * 1000.0 >= AimMaxMs) EndLocalAim(true);   // 최대 유지 시간: 서버보다 먼저 끈다
		else if (ComputeAim(*AimSkill, Aim)) ShowSkillIndicator(*AimSkill, Aim, -1.f);
	}
	TickSkillIndicator();

	// 사망 중에는 이동을 보내지 않는다(서버도 버린다, D-149). 부활 때 SC_REVIVE의 위치·순번에서 다시 시작한다.
	// 레벨 전환 대기 중에도 보내지 않는다(서버는 로딩 대기 중의 이동을 버린다, DG3c-2).
	if (bIsDead || bTravelLocked)
	{
		DesiredInput = FVector2D::Zero();
		LastDesiredInput = DesiredInput;
		bMoveSendPending = false;
		bWasMoving = false;
		return;
	}

	// 개발용 자동 공격: 시전할 수 있게 되는 첫 프레임에 누른 것처럼 시전한다(근소한 거부 측정, D-213).
	if (DevAutoRemain > 0)
	{
		const uint32 Before = AttackSeq;
		CastSkillSlot(DevAutoSlot);
		if (AttackSeq != Before) --DevAutoRemain;
	}

	UNetworkManager* NetManager = UNetworkManager::Get(this);
	if (NetManager == nullptr) return;

	// 입력이 바뀌면(멈춤 포함) 바로 보낸다.
	if (LastDesiredInput != DesiredInput)
	{
		bMoveSendPending = true;
		LastDesiredInput = DesiredInput;
	}

	if (DesiredInput == FVector2D::Zero())
		ObjectInfo.mutable_position()->set_state(Protocol::MOVE_STATE_IDLE);
	else
		ObjectInfo.mutable_position()->set_state(Protocol::MOVE_STATE_RUN);

	// 입력을 놓아도 감속하며 조금 더 미끄러진다. 완전히 멈춘 순간의 위치를 한 번 더 보내 서버 위치를 맞춘다.
	const bool bMoving = DesiredInput != FVector2D::Zero() || GetVelocity().SizeSquared2D() > 1.f;
	if (bWasMoving && !bMoving) bMoveSendPending = true;

	// 속도 방향이 마지막 송신 때보다 MoveSendTurnDeg 넘게 바뀌면 바로 보낸다(가속 중 회전·마우스로 시점을 돌리며 이동). 원격 외삽 오차를 줄인다(MC4.1b).
	const FVector2D VelDir = FVector2D(GetVelocity()).GetSafeNormal();
	if (bMoving && !VelDir.IsZero() && !LastSentVelDir.IsZero() && FVector2D::DotProduct(VelDir, LastSentVelDir) < MoveSendTurnCos)
		bMoveSendPending = true;
	// 조준 중 제자리 회전(카메라 방향)도 원격에 보이도록 방향이 MoveSendTurnDeg 넘게 바뀌면 보낸다(MC4.2d-2)
	if (!bMoving && IsSkillAiming() && MoveSendTurnDegValue > 0.f &&
		FMath::Abs(FMath::FindDeltaAngleDegrees(LastSentYaw, GetActorRotation().Yaw)) > MoveSendTurnDegValue)
		bMoveSendPending = true;
	bWasMoving = bMoving;

	MovePacketSendTimer -= DeltaTime;
	TimeSinceMoveSend += DeltaTime;

	// 정지 중에는 보내지 않는다. 서버가 버리지 않도록 MoveMinInterval 안의 전송은 다음 틱으로 미룬다.
	const bool bPeriodic = bMoving && MovePacketSendTimer <= 0.f;
	if ((bMoveSendPending || bPeriodic) && TimeSinceMoveSend >= MoveMinInterval)
	{
		NetManager->SendMove(ObjectInfo.position(), bMoveSendPending, CorrectionSeq);
		FNetMoveStats::Get().AddSend();
		MovePacketSendTimer = MoveSendInterval;
		TimeSinceMoveSend = 0.f;
		bMoveSendPending = false;
		LastSentVelDir = VelDir;
		LastSentYaw = GetActorRotation().Yaw;
	}

}

void AJMMyPlayer::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (bIsDead) return;   // 사망 중 이동 입력 차단(D-149)

	// 방향키를 누르면 자동 걷기를 멈추고 직접 움직인다(UI6). 떼는 입력(0)은 자동 걷기를 건드리지 않는다.
	if (IsAutoWalking())
	{
		if (MovementVector.IsNearlyZero()) return;
		StopAutoWalk();
	}

	// 공격 중(휘두르기가 끝날 때까지)에는 이동 입력을 받지 않는다(D-199). 키를 계속 누르고 있으면 잠금이 풀리는 틱부터 다시 움직인다.
	if (IsMoveLocked())
	{
		DesiredInput = FVector2D::Zero();
		return;
	}

	if (GetController() != nullptr)
	{
		// 카메라(컨트롤러) 방향 기준 이동: W = 카메라가 바라보는 쪽, D = 카메라 오른쪽.
		// 둘러보기(Left Alt, UI6) 중에는 누른 순간의 카메라 방향을 기준으로 한다(카메라만 돌고 캐릭터는 하던 방향으로).
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, bFreeLook ? FreeLookMoveYaw : Rotation.Yaw, 0);

		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);

		DesiredInput = MovementVector;
	}
}

void AJMMyPlayer::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// 커서가 보이는 동안(커서 모드·창 열림, UI6)에는 마우스로 카메라를 돌리지 않는다.
	if (const UJMUIManager* UIManager = GetUIManager(); UIManager && UIManager->IsCursorShown()) return;

	if (GetController() != nullptr)
	{
		AddControllerYawInput(LookAxisVector.X * LookSensitivityYaw);
		AddControllerPitchInput(LookAxisVector.Y * LookSensitivityPitch);
		DesiredYaw = GetActorRotation().Yaw;
	}
}

void AJMMyPlayer::ApplyServerCorrection(const Protocol::PositionInfo& ServerPos, uint32 Seq)
{
	const FVector Target(ServerPos.x(), ServerPos.y(), ServerPos.z());
	UE_LOG(LogTemp, Log, TEXT("Move corrected by server: seq=%u, %.0f cm back to (%.0f, %.0f, %.0f)"),
		Seq, FVector::Dist(GetActorLocation(), Target), Target.X, Target.Y, Target.Z);

	CorrectionSeq = Seq;
	SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		MoveComp->StopMovementImmediately();

	// 보정 위치에서 다시 시작한다는 것을 새 순번과 함께 알린다.
	SyncTransformToInfo();
	bMoveSendPending = true;
}

void AJMMyPlayer::RequestAttack()
{
	// BP_PControler의 기존 좌클릭 노드가 호출한다. IA_Attack과 같은 경로로 처리한다.
	OnAttackInput();
}

const FSkillTableRow* AJMMyPlayer::FindMySkill(int32 Slot) const
{
	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	if (DataManager == nullptr) return nullptr;

	return DataManager->FindSkillBySlot(GetWeaponType(), Slot);
}

// 화면 중앙(크로스헤어)을 지나는 카메라 레이로 시전 방향/대상/지면 지점을 구한다.
bool AJMMyPlayer::ComputeAim(const FSkillTableRow& Skill, FAimResult& Out) const
{
	Out = FAimResult();
	if (GetController() == nullptr || GetWorld() == nullptr) return false;

	FVector CamLoc;
	FRotator CamRot;
	GetController()->GetPlayerViewPoint(CamLoc, CamRot);
	const FVector CamForward = CamRot.Vector();
	const FVector Self = GetActorLocation();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AimTrace), false, this);

	if (Skill.TargetingType == ESkillTargetingType::Targeted)
	{
		// 카메라와 캐릭터 사이의 물체는 무시하도록 캐릭터 위치쯤에서 시작한다. 크로스헤어 아래의 몬스터 중 가장 가까운 것을 고른다.
		const FVector Start = CamLoc + CamForward * CameraBoom->TargetArmLength;
		const FVector End = Start + CamForward * (Skill.CastRange + 200.f);

		constexpr float AimAssistRadius = 80.f;   // 크로스헤어가 정확히 맞지 않아도 잡히는 허용 반경
		TArray<FHitResult> Hits;
		GetWorld()->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(AimAssistRadius), Params);

		const ANpcCharacter* Best = nullptr;
		float BestDistSq = TNumericLimits<float>::Max();
		for (const FHitResult& Hit : Hits)
		{
			const ANpcCharacter* Npc = Cast<ANpcCharacter>(Hit.GetActor());
			if (Npc == nullptr || Npc->IsDeadActor()) continue;

			const float DistSq = FVector::DistSquaredXY(Self, Npc->GetActorLocation());
			if (DistSq > FMath::Square(Skill.CastRange)) continue;   // 사거리 밖
			if (DistSq < BestDistSq) { BestDistSq = DistSq; Best = Npc; }
		}
		if (Best == nullptr)
		{
			// 근접(투사체 속도 0)은 대상이 없어도 카메라 방향으로 허공에 휘두른다(SK2, D-278 — 서버도 대상 0을 헛스윙으로 받음).
			// 원거리(활 기본 공격)는 지금처럼 아무 일도 하지 않는다.
			if (Skill.ProjSpeed > 0.f) return false;
			Out.TargetId = 0;
			Out.Yaw = CamRot.Yaw;
			Out.Point = FVector2D(Self.X, Self.Y);
			Out.bValid = true;
			return true;
		}

		const FVector Dir = Best->GetActorLocation() - Self;
		Out.TargetId = Best->GetObjectInfo().id();
		Out.Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
		Out.Point = FVector2D(Best->GetActorLocation().X, Best->GetActorLocation().Y);
		Out.bValid = true;
		return true;
	}

	if (Skill.HitShape == ESkillHitShape::Zone)
	{
		// 크로스헤어가 가리키는 지면 지점. 트레이스가 지면을 못 맞히면(콜리전이 Visibility를 무시하는 바닥 등)
		// 발밑 높이 평면과 카메라 레이의 교점을 쓴다. 평면과도 만나지 않으면(하늘을 볼 때) 정면 사거리 끝으로 한다.
		FVector Target;
		FHitResult Hit;
		const float FeetZ = Self.Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		if (GetWorld()->LineTraceSingleByChannel(Hit, CamLoc, CamLoc + CamForward * 20000.f, ECC_Visibility, Params))
		{
			Target = Hit.ImpactPoint;
		}
		else if (CamForward.Z < -KINDA_SMALL_NUMBER && CamLoc.Z > FeetZ)
		{
			Target = CamLoc + CamForward * ((FeetZ - CamLoc.Z) / CamForward.Z);
		}
		else
		{
			const FVector Flat = FVector(CamForward.X, CamForward.Y, 0.f).GetSafeNormal();
			Target = Self + Flat * Skill.CastRange;
		}

		// 사거리를 넘으면 사거리 끝으로 제한한다. (서버도 같은 보정을 한다)
		FVector2D Offset(Target.X - Self.X, Target.Y - Self.Y);
		const float Dist = Offset.Size();
		if (Dist > Skill.CastRange && Dist > KINDA_SMALL_NUMBER)
			Offset *= Skill.CastRange / Dist;

		Out.Point = FVector2D(Self.X + Offset.X, Self.Y + Offset.Y);
		Out.Yaw = (Dist > KINDA_SMALL_NUMBER) ? FMath::RadiansToDegrees(FMath::Atan2(Offset.Y, Offset.X)) : CamRot.Yaw;
		Out.bValid = true;
		return true;
	}

	// 방향 지정(부채꼴/직선 등): 카메라가 바라보는 방향
	Out.Yaw = CamRot.Yaw;
	Out.bValid = true;
	return true;
}

bool AJMMyPlayer::IsMoveLocked() const
{
	return GetWorld() != nullptr && GetWorld()->GetTimeSeconds() < MoveLockUntil;
}

void AJMMyPlayer::ShowSkillIndicator(const FSkillTableRow& Skill, const FAimResult& Aim, float FillSec)
{
	// 대상 지정(근접·활 기본 공격)과 대상 투사체는 범위가 없으니 표시하지 않는다.
	if (SkillIndicator == nullptr || Skill.TargetingType == ESkillTargetingType::Targeted || Skill.HitShape == ESkillHitShape::Projectile)
	{
		if (FillSec >= 0.f) HideSkillIndicator();
		return;
	}
	if (SkillIndicatorMID == nullptr)
	{
		UMaterialInterface* Mat = SkillIndicatorMaterial.LoadSynchronous();
		if (Mat == nullptr) return;
		SkillIndicator->SetDecalMaterial(Mat);
		SkillIndicatorMID = SkillIndicator->CreateDynamicMaterialInstance();
		if (SkillIndicatorMID == nullptr) return;
	}

	const float FeetZ = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector2D Self(GetActorLocation().X, GetActorLocation().Y);
	const FVector2D Dir(FMath::Cos(FMath::DegreesToRadians(Aim.Yaw)), FMath::Sin(FMath::DegreesToRadians(Aim.Yaw)));

	// 모양: 0 = 원·부채꼴(가운데가 시전자 또는 장판 지점), 1 = 직사각형(Pierce·Line — 시전자에서 앞으로 Range, 폭 Width)
	float Shape = 0.f, AngleDeg = 360.f, HalfF = 0.f, HalfS = 0.f, Yaw = Aim.Yaw;
	FVector2D Center = Self;
	if (Skill.HitShape == ESkillHitShape::Zone)
	{
		Center = Aim.Point;
		HalfF = HalfS = Skill.Radius;
		Yaw = 0.f;
	}
	else if (Skill.HitShape == ESkillHitShape::Sector)
	{
		AngleDeg = Skill.Angle;
		HalfF = HalfS = Skill.Radius;
	}
	else
	{
		Shape = 1.f;
		const float Length = FMath::Max(Skill.Range, Skill.CastRange);
		HalfF = Length * 0.5f;
		HalfS = FMath::Max(Skill.Width * 0.5f, 10.f);
		Center = Self + Dir * HalfF;
	}
	if (HalfF <= 0.f) return;

	constexpr float BorderCm = 8.f;
	const float Extent = FMath::Max(HalfF, HalfS) + 20.f;
	SkillIndicator->DecalSize = FVector(120.f, Extent, Extent);   // (위아래 투영 깊이, 바닥 두 축) — 깊이를 얕게 해 캐릭터 몸에 덜 묻게
	SkillIndicator->SetWorldLocation(FVector(Center.X, Center.Y, FeetZ));
	SkillIndicator->SetWorldRotation(FRotator(-90.f, 0.f, 0.f));
	SkillIndicatorMID->SetScalarParameterValue(TEXT("CenterX"), Center.X);
	SkillIndicatorMID->SetScalarParameterValue(TEXT("CenterY"), Center.Y);
	SkillIndicatorMID->SetScalarParameterValue(TEXT("Yaw"), Yaw);
	SkillIndicatorMID->SetScalarParameterValue(TEXT("HalfF"), HalfF);
	SkillIndicatorMID->SetScalarParameterValue(TEXT("HalfS"), HalfS);
	SkillIndicatorMID->SetScalarParameterValue(TEXT("Shape"), Shape);
	SkillIndicatorMID->SetScalarParameterValue(TEXT("AngleDeg"), AngleDeg);
	SkillIndicatorMID->SetScalarParameterValue(TEXT("BorderU"), FMath::Min(0.5f, BorderCm / HalfF));
	SkillIndicatorMID->SetScalarParameterValue(TEXT("BorderV"), FMath::Min(0.5f, BorderCm / HalfS));

	if (FillSec >= 0.f)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		IndicatorFillStart = Now;
		IndicatorFillEnd = Now + FMath::Max(FillSec, 0.05f);
		bIndicatorFilling = true;
		SkillIndicatorMID->SetScalarParameterValue(TEXT("Fill"), 0.001f);
	}
	else
	{
		bIndicatorFilling = false;
		SkillIndicatorMID->SetScalarParameterValue(TEXT("Fill"), 0.f);
	}
	SkillIndicator->SetVisibility(true);
}

void AJMMyPlayer::HideSkillIndicator()
{
	bIndicatorFilling = false;
	if (SkillIndicator) SkillIndicator->SetVisibility(false);
}

void AJMMyPlayer::TickSkillIndicator()
{
	if (SkillIndicator == nullptr || !SkillIndicator->IsVisible()) return;
	if (bIndicatorFilling)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		const float Alpha = static_cast<float>((Now - IndicatorFillStart) / FMath::Max(IndicatorFillEnd - IndicatorFillStart, 0.01));
		if (SkillIndicatorMID) SkillIndicatorMID->SetScalarParameterValue(TEXT("Fill"), FMath::Clamp(Alpha, 0.001f, 1.f));
		if (Now > IndicatorFillEnd + 0.15) HideSkillIndicator();   // 타격 순간 꽉 찬 모습을 잠깐 보여 주고 끈다
	}
	else if (!bAiming)
	{
		HideSkillIndicator();
	}
}

UCombatHudWidget* AJMMyPlayer::GetCombatHud() const
{
	UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr;
	return UIManager ? Cast<UCombatHudWidget>(UIManager->GetWidget(WidgetNames::HUD)) : nullptr;
}

bool AJMMyPlayer::CanCastLocally(int32 SkillId)
{
	if (bIsDead || GetWorld() == nullptr) return false;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < LocalActionUntil) return false;   // 앞 동작이 끝나지 않았다(서버가 ACTING으로 거부한다)
	if (const double* CooldownEnd = SkillCooldownEnd.Find(SkillId); CooldownEnd && FPlatformTime::Seconds() < *CooldownEnd) return false;
	if (PendingCast.bActive)
	{
		if (Now - PendingCast.StartTime < PendingCastTimeoutSec) return false;
		UE_LOG(LogTemp, Warning, TEXT("[Cast] 응답 없음 seq=%u skill=%d — 대기 해제"), PendingCast.Seq, PendingCast.SkillId);
		PendingCast.bActive = false;
		++StatCastTimeout;
	}
	return true;
}

void AJMMyPlayer::CastSkillSlot(int32 Slot)
{
	const FSkillTableRow* Skill = FindMySkill(Slot);
	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const int32 SkillId = DataManager ? DataManager->FindSkillIdBySlot(GetWeaponType(), Slot) : 0;
	if (Skill == nullptr || SkillId == 0)
	{
		EndLocalAim(true);
		return;
	}

	// 로컬 사전 검사(CP1, D-213): 서버가 거부할 것이 분명한 요청은 보내지 않는다. 조준 중이면 조준을 유지한다.
	if (!CanCastLocally(SkillId)) return;

	FAimResult Aim;
	if (!ComputeAim(*Skill, Aim)) return;   // 대상 없음: 조준 중이면 조준을 유지한다.

	UNetworkManager* NetManager = UNetworkManager::Get(this);
	if (NetManager == nullptr) return;

	const double Now = GetWorld()->GetTimeSeconds();
	const double* PrevCooldown = SkillCooldownEnd.Find(SkillId);

	PendingCast = FPendingCast();
	PendingCast.bActive = true;
	PendingCast.Seq = ++AttackSeq;
	PendingCast.SkillId = SkillId;
	PendingCast.Slot = Slot;
	PendingCast.StartTime = Now;
	PendingCast.PrevCooldownEnd = PrevCooldown ? *PrevCooldown : 0.0;
	PendingCast.PrevLocalActionUntil = LocalActionUntil;
	PendingCast.PrevMoveLockUntil = MoveLockUntil;
	++StatCastSent;

	NetManager->SendAttack(Slot, Aim.TargetId, Aim.Yaw, Aim.Point.X, Aim.Point.Y, PendingCast.Seq);

	// 여기부터 서버 답을 기다리지 않고 바로 시작한다(CP1, D-212). 서버가 거부하면 RollbackCast가 되돌린다.
	// 실제 판정 방향·피해는 서버가 확정한다.
	SetActorRotation(FRotator(0.f, Aim.Yaw, 0.f));
	LockFacingForSkill(*Skill);

	// 이동 잠금 + 범위 표시(D-199). 동작 시간은 서버와 같은 길이(AnimMs ÷ 재생 배율)로 잰다.
	const float Rate = FMath::Max(GetAttackPlayRate(Skill), 0.01f);
	LocalActionUntil = Now + Skill->AnimMs / 1000.0 / Rate;
	const float LockSec = Skill->MoveLockMs / 1000.f / Rate;
	if (LockSec > 0.f)
	{
		MoveLockUntil = Now + LockSec;
		DesiredInput = FVector2D::Zero();
		if (UCharacterMovementComponent* MoveComp = GetCharacterMovement()) MoveComp->StopMovementImmediately();
		// 몸 방향도 같은 시간 동안 고정한다(잠금이 풀리기 전에 이동 방향으로 몸 전체가 돌지 않게).
		LockFacing(LockSec);
		// 멈춘 자리를 바로 알린다(서버는 잠금 동안 ServerMoveLockSlackCm까지만 받는다).
		SyncTransformToInfo();
		bMoveSendPending = true;
	}
	// 판정이 나는 순간까지 판정 범위를 바닥에 채워 보여 준다(SK2 데칼).
	ShowSkillIndicator(*Skill, Aim, FMath::Max(Skill->HitDelayMs / 1000.f / Rate, 0.15f));

	// 서버는 CS_ATTACK을 받으며 조준을 해제한다(성공이든 실패든). 끄기를 따로 보내지 않는다.
	EndLocalAim(false);

	// 몽타주·무기 꺼내기
	OnAttack(Skill);
	if (UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		PendingCast.Montage = AnimInst->GetCurrentActiveMontage();

	// 이펙트(장판은 내가 조준한 지점 — 서버도 같은 식으로 사거리 보정을 한다)
	if (UJMObjectManager* ObjectManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMObjectManager>() : nullptr)
	{
		PendingCast.Projectile = MakeShared<TWeakObjectPtr<AActor>>();
		ObjectManager->SpawnSkillFx(this, *Skill, Aim.TargetId, Aim.Yaw, Aim.Point.X, Aim.Point.Y, &PendingCast.FxComp, &PendingCast.FxTimer, PendingCast.Projectile);
	}

	// 쿨타임 표시(길이는 SkillTable 값 = 서버가 적용하는 값)
	if (Skill->CooldownMs > 0)
	{
		if (UCombatHudWidget* Hud = GetCombatHud()) Hud->StartCooldown(Skill->Slot, Skill->CooldownMs);
		NoteSkillCooldown(SkillId, Skill->CooldownMs);
	}

	UE_LOG(LogTemp, Verbose, TEXT("[Cast] 시작 seq=%u slot=%d skill=%d"), PendingCast.Seq, Slot, SkillId);
}

bool AJMMyPlayer::ConfirmCast(uint32 Seq, int32 SkillId, float AimX, float AimY)
{
	if (Seq == 0) return false;   // 내가 먼저 시작한 시전이 아니다

	if (!PendingCast.bActive || PendingCast.Seq != Seq)
		return true;   // 이미 정리된 시전의 늦은 확정: 다시 재생하지 않는다

	if (PendingCast.SkillId != SkillId)
	{
		// 서버가 다른 스킬로 확정했다(무기가 서로 달랐던 순간). 먼저 시작한 것을 되돌리고 서버 값으로 재생하게 한다.
		UE_LOG(LogTemp, Warning, TEXT("[Cast] 확정 스킬 불일치 seq=%u local=%d server=%d"), Seq, PendingCast.SkillId, SkillId);
		UndoPendingCast();
		return false;
	}

	// 서버가 장판 지점을 다르게 보정했으면 이펙트를 그 자리로 옮긴다.
	if (UParticleSystemComponent* FxComp = PendingCast.FxComp.Get())
	{
		const FVector Loc = FxComp->GetComponentLocation();
		if (FVector2D::DistSquared(FVector2D(Loc), FVector2D(AimX, AimY)) > FMath::Square(10.f))
			FxComp->SetWorldLocation(FVector(AimX, AimY, Loc.Z));
	}

	UE_LOG(LogTemp, Verbose, TEXT("[Cast] 확정 seq=%u skill=%d (%.0fms)"), Seq, SkillId, GetWorld() ? (GetWorld()->GetTimeSeconds() - PendingCast.StartTime) * 1000.0 : 0.0);
	PendingCast.bActive = false;
	++StatCastConfirmed;
	return true;
}

void AJMMyPlayer::UndoPendingCast()
{
	if (!PendingCast.bActive) return;
	PendingCast.bActive = false;

	// 몽타주: 먼저 재생한 그 몽타주만 멈춘다(그 사이 시작된 피격·사망 몽타주는 건드리지 않는다).
	if (UAnimMontage* Montage = PendingCast.Montage.Get())
	{
		if (UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
			if (AnimInst->Montage_IsPlaying(Montage)) AnimInst->Montage_Stop(0.1f, Montage);
	}

	// 이펙트
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(PendingCast.FxTimer);
	if (UParticleSystemComponent* FxComp = PendingCast.FxComp.Get()) FxComp->DestroyComponent();
	if (PendingCast.Projectile.IsValid())
		if (AActor* Proj = PendingCast.Projectile->Get()) Proj->Destroy();

	// 쿨타임: 시전 전 값으로
	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FSkillTableRow* Skill = DataManager ? DataManager->FindSkillById(PendingCast.SkillId) : nullptr;
	const double PlatformNow = FPlatformTime::Seconds();
	if (PendingCast.PrevCooldownEnd > PlatformNow) SkillCooldownEnd.Add(PendingCast.SkillId, PendingCast.PrevCooldownEnd);
	else SkillCooldownEnd.Remove(PendingCast.SkillId);
	// HUD 칸은 무기가 그대로일 때만 고친다(무기를 바꿨으면 그 칸은 이미 다른 스킬이다).
	if (Skill && Skill->CooldownMs > 0 && DataManager->FindSkillIdBySlot(GetWeaponType(), PendingCast.Slot) == PendingCast.SkillId)
	{
		const int32 RemainMs = FMath::Max(0, FMath::RoundToInt32((PendingCast.PrevCooldownEnd - PlatformNow) * 1000.0));
		if (UCombatHudWidget* Hud = GetCombatHud()) Hud->SetCooldownRemain(Skill->Slot, RemainMs, Skill->CooldownMs);
	}

	// 잠금·방향 고정·범위 표시
	LocalActionUntil = PendingCast.PrevLocalActionUntil;
	MoveLockUntil = PendingCast.PrevMoveLockUntil;
	if (GetWorldTimerManager().IsTimerActive(FacingLockTimer))
	{
		GetWorldTimerManager().ClearTimer(FacingLockTimer);
		UnlockFacing();
	}
	HideSkillIndicator();
}

void AJMMyPlayer::RollbackCast(uint32 Seq, int32 Result, int32 RemainMs)
{
	++StatCastRejected;
	if (Result >= 0 && Result < static_cast<int32>(UE_ARRAY_COUNT(StatCastRejectedBy))) ++StatCastRejectedBy[Result];

	if (!PendingCast.bActive || PendingCast.Seq != Seq)
	{
		UE_LOG(LogTemp, Log, TEXT("[Cast] 거부(이미 정리됨) seq=%u result=%d"), Seq, Result);
		return;
	}

	const int32 SkillId = PendingCast.SkillId;
	const int32 Slot = PendingCast.Slot;
	UE_LOG(LogTemp, Log, TEXT("[Cast] 거부 → 되돌림 seq=%u skill=%d result=%d remain=%dms (%.0fms 뒤)"), Seq, SkillId, Result, RemainMs,
		GetWorld() ? (GetWorld()->GetTimeSeconds() - PendingCast.StartTime) * 1000.0 : 0.0);
	UndoPendingCast();

	// MP 부족(A2.3b, D-254): 되돌리기만 한다(남은 시간 없음). 안내 문구·HUD 표시는 MP HUD 항목에서
	if (Result == Protocol::ATTACK_RESULT_NOT_ENOUGH_MP)
	{
		UE_LOG(LogTemp, Log, TEXT("[Cast] MP 부족 skill=%d mp=%d/%d"), SkillId, GetObjectInfo().stat().mp(), GetObjectInfo().stat().max_mp());
		return;
	}

	// 서버 기준 남은 시간에 로컬 시각을 맞춘다(그 전에는 다시 보내지 않게).
	if (RemainMs <= 0) return;
	if (Result == Protocol::ATTACK_RESULT_ACTING)
	{
		if (GetWorld()) LocalActionUntil = FMath::Max(LocalActionUntil, GetWorld()->GetTimeSeconds() + RemainMs / 1000.0);
	}
	else if (Result == Protocol::ATTACK_RESULT_COOLDOWN)
	{
		SkillCooldownEnd.Add(SkillId, FPlatformTime::Seconds() + RemainMs / 1000.0);
		UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
		const FSkillTableRow* Skill = DataManager ? DataManager->FindSkillById(SkillId) : nullptr;
		if (Skill && DataManager->FindSkillIdBySlot(GetWeaponType(), Slot) == SkillId)
			if (UCombatHudWidget* Hud = GetCombatHud()) Hud->SetCooldownRemain(Skill->Slot, RemainMs, Skill->CooldownMs);
	}
}

void AJMMyPlayer::DevAutoAttack(int32 Count, int32 Slot)
{
	DevAutoRemain = FMath::Max(Count, 0);
	DevAutoSlot = Slot;
}

FString AJMMyPlayer::DevAttackStats(bool bReset)
{
	FString By;
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(StatCastRejectedBy)); ++Index)
		if (StatCastRejectedBy[Index] > 0) By += FString::Printf(TEXT(" %d=%d"), Index, StatCastRejectedBy[Index]);
	const FString Out = FString::Printf(TEXT("[AttackStats] sent=%d confirmed=%d rejected=%d timeout=%d autoRemain=%d byResult:%s"),
		StatCastSent, StatCastConfirmed, StatCastRejected, StatCastTimeout, DevAutoRemain, By.IsEmpty() ? TEXT(" none") : *By);
	if (bReset)
	{
		StatCastSent = StatCastConfirmed = StatCastRejected = StatCastTimeout = 0;
		for (int32& Value : StatCastRejectedBy) Value = 0;
	}
	return Out;
}

void AJMMyPlayer::OnAttackInput()
{
	// BP 노드와 IA_Attack이 같은 프레임에 겹쳐 들어와도 한 번만 처리한다.
	if (LastAttackInputFrame == GFrameCounter) return;
	LastAttackInputFrame = GFrameCounter;

	if (bIsDead) return;   // 사망 중 공격 입력 차단(D-149, 서버도 거부한다)

	// 커서가 보일 때(커서 모드·창 열림, UI6 D-274) 월드 좌클릭은 공격하지 않는다(창 위 클릭은 UI가 먼저 받아 여기 오지 않음).
	// 스킬 조준 중의 좌클릭 확정은 그대로 받는다(창을 연 채 숫자키로 조준한 경우).
	if (!bAiming)
	{
		if (UJMUIManager* UIManager = GetUIManager(); UIManager && UIManager->IsCursorShown())
		{
			float MouseX = 0.f, MouseY = 0.f;
			const APlayerController* PC = Cast<APlayerController>(GetController());
			if (PC) PC->GetMousePosition(MouseX, MouseY);
			HandleCursorClick(false, FVector2D(MouseX, MouseY));
			return;
		}
	}

	StopAutoWalk();
	CastSkillSlot(bAiming ? AimingSlot : 0);
}

void AJMMyPlayer::OnQuickSlotInput(int32 Index)
{
	if (!QuickSlots.IsValidIndex(Index)) return;
	const FQuickSlot& QuickSlot = QuickSlots[Index];

	switch (QuickSlot.Kind)
	{
	case EQuickSlotKind::Skill:
		// 커서 모드(UI6, D-274)에서 숫자키 = 커서 모드를 끄고 바로 조준한다(열린 창은 그대로). 자동 걷기도 멈춘다.
		if (UJMUIManager* UIManager = GetUIManager()) UIManager->SetCursorMode(false);
		StopAutoWalk();
		// 스킬 칸: 조준 진입. 조준 중에 같은 스킬 칸을 다시 누르면 취소한다.
		if (bAiming && AimingSlot == QuickSlot.Value)
		{
			EndLocalAim(true);
			return;
		}
		BeginLocalAim(QuickSlot.Value);
		break;
	case EQuickSlotKind::Item:
		// 아이템 칸(A3.6): 그 종류를 하나 쓰겠다고 요청한다(서버가 수량이 적은 스택부터 고른다).
		// 검증(보유·HP·재사용 시간·사망)은 서버가 하고 결과는 SC_USE_ITEM·SC_INVENTORY_UPDATE, 거부는 시스템 메시지로 온다.
		if (UNetworkManager* NetManager = UNetworkManager::Get(this))
		{
			NetManager->SendUseItem(0, QuickSlot.Value);
		}
		break;
	default:
		break;
	}
}

void AJMMyPlayer::RebuildQuickSlots()
{
	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const UJMInventoryManager* Inventory = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMInventoryManager>() : nullptr;
	const EWeaponType WeaponType = GetWeaponType();

	QuickSlots.Reset(QuickSlotActions.Num());
	for (int32 Index = 0; Index < QuickSlotActions.Num(); ++Index)
	{
		const int32 SkillSlot = Index + 1;   // 칸 N = SkillTable 슬롯 N(0은 좌클릭 기본 공격)
		const bool bHasSkill = DataManager && DataManager->FindSkillBySlot(WeaponType, SkillSlot) != nullptr;
		// 스킬이 우선이고, 없으면 서버에 저장된 배치의 아이템 종류(D-154).
		const int32 ItemId = Inventory ? Inventory->GetQuickSlotItem(Index) : 0;
		if (bHasSkill) QuickSlots.Add(FQuickSlot::MakeSkill(SkillSlot));
		else if (ItemId != 0) QuickSlots.Add(FQuickSlot::MakeItem(ItemId));
		else QuickSlots.Add(FQuickSlot());
	}
}

void AJMMyPlayer::OnCancelAimInput()
{
	EndLocalAim(true);
}

void AJMMyPlayer::OnRightClickInput()
{
	// 스킬 조준 중의 우클릭 = 조준 취소(D-022) — 커서 상호작용보다 먼저
	if (bAiming)
	{
		OnCancelAimInput();
		return;
	}
	UJMUIManager* UIManager = GetUIManager();
	if (UIManager == nullptr || !UIManager->IsCursorShown()) return;   // 카메라 모드의 우클릭은 아무 일도 하지 않는다
	float MouseX = 0.f, MouseY = 0.f;
	if (const APlayerController* PC = Cast<APlayerController>(GetController())) PC->GetMousePosition(MouseX, MouseY);
	HandleCursorClick(true, FVector2D(MouseX, MouseY));
}

void AJMMyPlayer::UpdateInteractPrompt()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance == nullptr) return;

	// 줍기 거리는 서버 값(SC_INVENTORY.pickup_range_cm)이다. 사망 중에는 주울 수 없다(서버도 거부).
	// 상점 NPC(L4)도 같은 F 키다: 이용 거리(SC_INVENTORY.shop_range_cm) 안의 가장 가까운 NPC와 드랍 중 가까운 쪽 하나만 안내한다.
	ADropItemActor* Nearest = nullptr;
	int32 NearestNpcId = 0;
	const UJMObjectManager* ObjectManager = GameInstance->GetSubsystem<UJMObjectManager>();
	UJMShopManager* Shop = GameInstance->GetSubsystem<UJMShopManager>();
	if (!bIsDead)
	{
		const UJMInventoryManager* Inventory = GameInstance->GetSubsystem<UJMInventoryManager>();
		const float Range = Inventory ? static_cast<float>(Inventory->GetPickupRangeCm()) : 0.f;
		if (ObjectManager && Range > 0.f) Nearest = ObjectManager->FindNearestDrop(GetActorLocation(), Range);

		// 경계에서 서버가 TOO_FAR로 거부하지 않게 안내는 이용 거리보다 조금 안쪽에서만 띄운다.
		const float ShopRange = Inventory ? static_cast<float>(Inventory->GetShopRangeCm()) - 30.f : 0.f;
		float NpcDistSq = 0.f;
		// 상점·경매 NPC 모두 같은 이용 거리다(D-234). 하는 일이 없는 NPC는 안내하지 않는다.
		if (ObjectManager && ShopRange > 0.f) NearestNpcId = ObjectManager->FindNearestNpc(GetActorLocation(), ShopRange, NpcDistSq);
		if (NearestNpcId != 0 && ObjectManager->GetNpcKind(NearestNpcId) == static_cast<int32>(Protocol::NPC_KIND_NONE)) NearestNpcId = 0;
		if (NearestNpcId != 0 && Nearest != nullptr)
		{
			if (FVector::DistSquared2D(GetActorLocation(), Nearest->GetActorLocation()) <= NpcDistSq) NearestNpcId = 0;
			else Nearest = nullptr;
		}
	}

	// 포털(L5): 드랍·NPC가 없을 때만. 반경은 서버 값(SC_PORTAL_LIST)이고 시전·이동 잠금 중에는 서버가 받지 않는다.
	int32 NearestPortalId = 0;
	int32 NearestPortalKind = 0;
	if (!bIsDead && ObjectManager && Nearest == nullptr && NearestNpcId == 0)
	{
		float PortalDistSq = 0.f;
		NearestPortalId = ObjectManager->FindNearestPortal(GetActorLocation(), PortalDistSq, &NearestPortalKind);
	}

	// 열려 있는 상점 창: 멀어지거나 죽거나 거래가 시작되면 닫는다.
	if (Shop) Shop->UpdateFromPlayer(GetActorLocation(), bIsDead);
	// 열려 있는 경매 창(A6.3b): 죽거나 거래·상점이 열리면, NPC 앞에서 연 것이면 멀어져도 닫는다.
	if (UJMAuctionManager* Auction = GameInstance->GetSubsystem<UJMAuctionManager>()) Auction->UpdateFromPlayer(GetActorLocation(), bIsDead);
	// 대화 모드 퀘스트 창(Q1): 죽거나 NPC에게서 멀어지면 닫는다.
	if (UJMQuestManager* Quest = GameInstance->GetSubsystem<UJMQuestManager>()) Quest->UpdateFromPlayer(GetActorLocation(), bIsDead);

	const bool bShown = Nearest != nullptr || NearestNpcId != 0 || NearestPortalId != 0;
	if (Nearest == InteractTarget.Get() && NearestNpcId == InteractNpcId && NearestPortalId == InteractPortalId && bInteractPromptShown == bShown) return;
	InteractTarget = Nearest;
	InteractNpcId = NearestNpcId;
	InteractPortalId = NearestPortalId;
	bInteractPromptShown = bShown;

	UJMUIManager* UIManager = GameInstance->GetSubsystem<UJMUIManager>();
	if (UCombatHudWidget* Hud = UIManager ? Cast<UCombatHudWidget>(UIManager->GetWidget(WidgetNames::HUD)) : nullptr)
	{
		FText Prompt;
		if (Nearest) Prompt = FText::FromString(FString::Printf(TEXT("[F] 줍기: %s"), *Nearest->GetDisplayText().ToString()));
		else if (NearestNpcId != 0 && ObjectManager && ObjectManager->GetNpcKind(NearestNpcId) == static_cast<int32>(Protocol::NPC_KIND_AUCTION))
			Prompt = FText::FromString(FString::Printf(TEXT("[F] 경매장: %s"), *ObjectManager->GetNpcName(NearestNpcId)));
		else if (NearestNpcId != 0 && ObjectManager && ObjectManager->GetNpcKind(NearestNpcId) == static_cast<int32>(Protocol::NPC_KIND_QUEST))
			Prompt = FText::FromString(FString::Printf(TEXT("[F] 대화: %s"), *ObjectManager->GetNpcName(NearestNpcId)));
		else if (NearestNpcId != 0 && ObjectManager) Prompt = FText::FromString(FString::Printf(TEXT("[F] 상점: %s"), *ObjectManager->GetNpcName(NearestNpcId)));
		// 포털 종류(SC_PORTAL_LIST의 kind — 서버가 정함, DG3c-2): 던전 입구·출구는 문구를 따로 둔다.
		else if (NearestPortalId != 0 && NearestPortalKind == Protocol::PORTAL_KIND_DUNGEON_ENTER) Prompt = FText::FromString(TEXT("[F] 던전 입장"));
		else if (NearestPortalId != 0 && NearestPortalKind == Protocol::PORTAL_KIND_DUNGEON_EXIT) Prompt = FText::FromString(TEXT("[F] 던전 나가기"));
		else if (NearestPortalId != 0) Prompt = FText::FromString(TEXT("[F] 포털 이동"));
		Hud->SetInteractPrompt(Prompt);
	}
}

void AJMMyPlayer::OnInteractInput()
{
	if (bIsDead) return;
	StopAutoWalk();
	// 누르는 순간의 위치로 다시 고른다(안내는 0.1초 전 것일 수 있다). 거리·소유권·빈 칸 검증은 서버가 하고 거부는 시스템 메시지로 온다.
	UpdateInteractPrompt();
	if (InteractNpcId != 0 && InteractWithNpc(InteractNpcId)) return;
	// 포털(L5): 요청만 보낸다. 받아들여지면 SC_TELEPORT가 오고, 거부(시전 중·재사용 대기 등)는 응답이 없다.
	if (InteractPortalId != 0)
	{
		if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendPortal(InteractPortalId);
		return;
	}
	const ADropItemActor* Target = InteractTarget.Get();
	if (Target == nullptr) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this))
	{
		NetManager->SendPickupItem(Target->GetObjectId());
	}
}

bool AJMMyPlayer::InteractWithNpc(int32 NpcId, bool bToggle)
{
	UGameInstance* GameInstance = GetGameInstance();
	const UJMObjectManager* NpcObjects = GameInstance ? GameInstance->GetSubsystem<UJMObjectManager>() : nullptr;
	if (NpcId == 0 || NpcObjects == nullptr) return false;
	const int32 Kind = NpcObjects->GetNpcKind(NpcId);
	// 경매 NPC(A6.3b, D-234): 등록·취소·수령까지 되는 NPC 모드로 연다. 이미 NPC 모드로 열려 있으면 닫는다(K 키로 연 조회 모드면 NPC 모드로 바꾼다).
	if (Kind == static_cast<int32>(Protocol::NPC_KIND_AUCTION))
	{
		if (UJMAuctionManager* Auction = GameInstance->GetSubsystem<UJMAuctionManager>())
		{
			if (Auction->IsAtNpc()) { if (bToggle) Auction->Close(); }
			else Auction->Open(NpcId);
		}
		return true;
	}
	// 퀘스트 NPC(Q1, D-265): 대화 모드 퀘스트 창을 연다. 이미 대화 모드로 열려 있으면 닫는다(L 키로 연 목록 모드면 대화 모드로 바꾼다).
	if (Kind == static_cast<int32>(Protocol::NPC_KIND_QUEST))
	{
		if (UJMQuestManager* Quest = GameInstance->GetSubsystem<UJMQuestManager>())
		{
			if (Quest->IsAtNpc()) { if (bToggle) Quest->Close(); }
			else Quest->OpenForNpc(NpcId);
		}
		return true;
	}
	// 상점 NPC(L4): 닫혀 있으면 품목을 요청하고(SC_SHOP_OPEN이 오면 창이 뜬다), 열려 있으면 닫는다. 거리·거래 중 검증은 서버가 한다.
	if (Kind == static_cast<int32>(Protocol::NPC_KIND_SHOP))
	{
		if (UJMShopManager* Shop = GameInstance->GetSubsystem<UJMShopManager>())
		{
			if (Shop->IsOpen()) { if (bToggle) Shop->Close(); }
			else Shop->RequestOpen(NpcId);
		}
		return true;
	}
	return false;
}

void AJMMyPlayer::OnPartyInput()
{
	if (UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr)
	{
		UIManager->ToggleWidget(WidgetNames::Party);
	}
}

void AJMMyPlayer::OnQuestInput()
{
	// L 키(Q1, D-265): 받은 퀘스트 목록(보기만). 열려 있으면(대화 모드 포함) 닫는다.
	UJMQuestManager* Quest = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMQuestManager>() : nullptr;
	if (Quest == nullptr) return;
	if (Quest->IsOpen()) { Quest->Close(); return; }
	Quest->OpenLog();
}

void AJMMyPlayer::OnAuctionInput()
{
	UJMAuctionManager* Auction = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMAuctionManager>() : nullptr;
	if (Auction == nullptr) return;
	if (Auction->IsOpen()) { Auction->Close(); return; }
	// 사망 중에는 열지 않는다(서버도 거부). 던전 안인지는 서버가 답한다(거부되면 안내가 뜨고 창이 닫힌다).
	if (bIsDead) return;
	Auction->Open(0);
}

void AJMMyPlayer::OnInventoryInput()
{
	// 창을 여닫기만 한다. 내용은 UJMInventoryManager가 가진 값(서버가 보낸 사본)이라 서버에 묻지 않는다.
	if (UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr)
	{
		UIManager->ToggleWidget(WidgetNames::Inventory);
	}
}

void AJMMyPlayer::BeginLocalAim(int32 Slot)
{
	if (bIsDead) return;
	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FSkillTableRow* Skill = FindMySkill(Slot);
	const int32 SkillId = DataManager ? DataManager->FindSkillIdBySlot(GetWeaponType(), Slot) : 0;
	if (Skill == nullptr || SkillId == 0) return;

	const double Now = FPlatformTime::Seconds();
	if (const double* CooldownEnd = SkillCooldownEnd.Find(SkillId); CooldownEnd && Now < *CooldownEnd) return;

	// 조준 단계가 없는 스킬(AimMaxMs 0)은 바로 시전한다.
	if (Skill->AimMaxMs <= 0)
	{
		EndLocalAim(true);
		CastSkillSlot(Slot);
		return;
	}

	if (Now - LastAimOnTime < AimMinInterval) return;   // 서버가 거부할 빈도

	if (bAiming) EndLocalAim(true);   // 다른 슬롯으로 바꿔 조준

	LastAimOnTime = Now;
	AimStartTime = Now;
	AimMaxMs = Skill->AimMaxMs;
	bAiming = true;
	AimingSlot = Slot;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this))
		NetManager->SendSkillAim(Slot, true, ++AimSeq);
	SetSkillAiming(SkillId);   // 이동 속도 배율을 바로 적용(서버 예산과 같은 배율)
}

void AJMMyPlayer::EndLocalAim(bool bSendOff)
{
	if (!bAiming) return;
	bAiming = false;
	if (bSendOff)
	{
		if (UNetworkManager* NetManager = UNetworkManager::Get(this))
			NetManager->SendSkillAim(AimingSlot, false, ++AimSeq);
	}
	SetSkillAiming(0);
}

void AJMMyPlayer::OnServerAimOff(uint32 Seq)
{
	if (bAiming && Seq == AimSeq)
	{
		UE_LOG(LogTemp, Log, TEXT("[SkillAim] 서버 해제(거부·시간 초과) seq=%u"), Seq);
		EndLocalAim(false);
	}
}

void AJMMyPlayer::CancelLocalAim()
{
	EndLocalAim(true);
}

void AJMMyPlayer::NoteSkillCooldown(int32 SkillId, int32 CooldownMs)
{
	if (CooldownMs > 0)
		SkillCooldownEnd.Add(SkillId, FPlatformTime::Seconds() + CooldownMs / 1000.0);
}

void AJMMyPlayer::OnDead()
{
	StopAutoWalk();
	ClosePlayerMenu();
	EndLocalAim(true);   // 서버는 사망 때 이미 해제했다(끄기는 무시됨)
	Super::OnDead();
}

void AJMMyPlayer::OnMyTeleport(const Protocol::PositionInfo& Pos, uint32 Seq)
{
	StopAutoWalk();
	TeleportToServerPosition(Pos);
	// 서버가 이동 때 보정 순번을 올렸다. 그 순번으로 도착 위치에서 다시 시작한다고 알린다(OnMyRevive와 같은 절차).
	CorrectionSeq = Seq;
	SyncTransformToInfo();
	bMoveSendPending = true;
}

void AJMMyPlayer::ResetForRoomChange()
{
	// 응답을 기다리던 시전은 서버가 받지 않은 것이다(받았다면 SC_ATTACK이 먼저 와서 이미 확정됐다): 몽타주·이펙트·쿨타임·잠금을 되돌린다.
	UndoPendingCast();

	// 조준은 서버가 이미 해제했으므로 끄기 패킷 없이 로컬에서만 끈다.
	EndLocalAim(false);
	HideSkillIndicator();

	// 이동·행동 잠금: 옛 Room에서 걸린 시간은 새 Room에 가져가지 않는다.
	MoveLockUntil = 0.0;
	LocalActionUntil = 0.0;

	// 개발용 자동 공격도 멈춘다.
	DevAutoRemain = 0;

	// 자동 걷기 대상·플레이어 메뉴 대상은 옛 Room의 것이다(UI6).
	StopAutoWalk();
	ClosePlayerMenu();

	// 상호작용 대상(드랍·NPC·포털)은 옛 Room의 것이므로 지우고 안내 문구도 지운다.
	// UpdateInteractPrompt는 대상이 같으면 HUD를 고치지 않으므로 여기서 직접 비워야 한다.
	InteractTarget.Reset();
	InteractNpcId = 0;
	InteractPortalId = 0;
	bInteractPromptShown = false;
	InteractCheckTimer = 0.f;
	if (UCombatHudWidget* Hud = GetCombatHud()) Hud->SetInteractPrompt(FText::GetEmpty());
}

void AJMMyPlayer::GetTravelState(uint32& OutAttackSeq, uint32& OutAimSeq, TMap<int32, double>& OutSkillCooldownEnd) const
{
	OutAttackSeq = AttackSeq;
	OutAimSeq = AimSeq;
	OutSkillCooldownEnd = SkillCooldownEnd;   // 끝 시각이 FPlatformTime::Seconds 기준이라 레벨이 바뀌어도 그대로 쓸 수 있다
}

void AJMMyPlayer::ApplyTravelState(uint32 InCorrectionSeq, uint32 InAttackSeq, uint32 InAimSeq, const TMap<int32, double>& InSkillCooldownEnd)
{
	// 서버는 Room 이동 때 보정 순번을 올리기만 하고 0으로 되돌리지 않는다. 새 액터가 0으로 시작하면 이동이 전부 버려진다(OnMyTeleport와 같은 절차).
	CorrectionSeq = InCorrectionSeq;
	SyncTransformToInfo();
	bMoveSendPending = true;

	// 요청 순번은 서버가 검사하지 않지만, 늦게 온 옛 응답과 겹치지 않게 이어 간다.
	AttackSeq = InAttackSeq;
	AimSeq = InAimSeq;

	// 쿨타임도 서버에서 이어진다. 로컬 판정과 HUD 표시를 남은 시간으로 맞춘다(액티브 칸 = 현재 무기의 스킬 슬롯).
	SkillCooldownEnd = InSkillCooldownEnd;
	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	UCombatHudWidget* Hud = GetCombatHud();
	if (DataManager == nullptr || Hud == nullptr) return;
	const double Now = FPlatformTime::Seconds();
	for (int32 Slot = 1; Slot <= QuickSlots.Num(); ++Slot)
	{
		const FSkillTableRow* Skill = FindMySkill(Slot);
		const int32 SkillId = DataManager->FindSkillIdBySlot(GetWeaponType(), Slot);
		const double* End = (Skill && SkillId != 0) ? SkillCooldownEnd.Find(SkillId) : nullptr;
		if (End == nullptr || *End <= Now) continue;
		Hud->SetCooldownRemain(Skill->Slot, FMath::CeilToInt32((*End - Now) * 1000.0), Skill->CooldownMs);
	}
}

void AJMMyPlayer::LockForLevelTravel()
{
	bTravelLocked = true;
	StopAutoWalk();
	DesiredInput = FVector2D::Zero();
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement()) MoveComp->StopMovementImmediately();
	if (APlayerController* PC = Cast<APlayerController>(GetController())) DisableInput(PC);
}

void AJMMyPlayer::OnMyRevive(const Protocol::PositionInfo& Pos, int32 Hp, uint32 Seq)
{
	OnRevive(Pos, Hp);
	// 서버가 부활 때 보정 순번을 올렸다. 그 순번으로 부활 위치에서 다시 시작한다고 알린다(ApplyServerCorrection과 같은 절차).
	CorrectionSeq = Seq;
	SyncTransformToInfo();
	bMoveSendPending = true;
}

// ---- 커서 모드 상호작용(UI6, D-274) ----

UJMUIManager* AJMMyPlayer::GetUIManager() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UJMUIManager>() : nullptr;
}

AJMMyPlayer::FPickResult AJMMyPlayer::PickAtScreen(const FVector2D& ScreenPos) const
{
	FPickResult Best;
	const APlayerController* PC = Cast<APlayerController>(GetController());
	const UGameInstance* GameInstance = GetGameInstance();
	const UJMObjectManager* ObjectManager = GameInstance ? GameInstance->GetSubsystem<UJMObjectManager>() : nullptr;
	if (PC == nullptr || ObjectManager == nullptr) return Best;

	// 허용 화면 거리는 세로 1080 기준 값을 지금 뷰포트 높이에 비례시킨다.
	int32 ViewX = 0, ViewY = 0;
	PC->GetViewportSize(ViewX, ViewY);
	float BestDist = PickScreenRadiusPx * (ViewY > 0 ? ViewY / 1080.f : 1.f);
	const FVector MyLocation = GetActorLocation();
	const float MaxDistSq = FMath::Square(PickMaxDistCm);
	const FVector Cursor(ScreenPos.X, ScreenPos.Y, 0.f);

	// 몸(발~머리)을 화면에 투영한 선분과 커서의 거리. 키가 큰 대상도 몸 어디를 눌러도 잡힌다(충돌 설정에 기대지 않음).
	auto Consider = [&](EPickKind Kind, int32 Id, const FVector& Bottom, const FVector& Top)
	{
		if (FVector::DistSquared(MyLocation, Bottom) > MaxDistSq) return;
		FVector2D A, B;
		if (!PC->ProjectWorldLocationToScreen(Bottom, A, true) || !PC->ProjectWorldLocationToScreen(Top, B, true)) return;
		const float Dist = FMath::PointDistToSegment(Cursor, FVector(A.X, A.Y, 0.f), FVector(B.X, B.Y, 0.f));
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best.Kind = Kind;
			Best.Id = Id;
		}
	};
	auto Body = [](const AJMCharacterBase* Character, FVector& OutBottom, FVector& OutTop)
	{
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.f;
		OutBottom = Character->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
		OutTop = Character->GetActorLocation() + FVector(0.f, 0.f, HalfHeight);
	};

	// NPC: 하는 일이 있는 것만(F 안내와 같은 기준)
	for (const TPair<int32, TObjectPtr<AJMCharacterBase>>& Pair : ObjectManager->Npcs)
	{
		if (Pair.Value == nullptr || ObjectManager->GetNpcKind(Pair.Key) == static_cast<int32>(Protocol::NPC_KIND_NONE)) continue;
		FVector Bottom, Top;
		Body(Pair.Value.Get(), Bottom, Top);
		Consider(EPickKind::Npc, Pair.Key, Bottom, Top);
	}
	// 다른 플레이어(살아 있는). 몬스터는 고르지 않는다(D-274)
	for (const TPair<int32, TObjectPtr<AJMCharacterBase>>& Pair : ObjectManager->Objects)
	{
		const AJMPlayer* Other = Cast<AJMPlayer>(Pair.Value.Get());
		if (Other == nullptr || Other == this || Other->IsDeadActor()) continue;
		FVector Bottom, Top;
		Body(Other, Bottom, Top);
		Consider(EPickKind::Player, Pair.Key, Bottom, Top);
	}
	// 포털: 서버가 준 위치(지면)에서 2m 위까지
	for (const UJMObjectManager::FPortalEntry& Portal : ObjectManager->Portals)
	{
		Consider(EPickKind::Portal, Portal.Id, Portal.Location, Portal.Location + FVector(0.f, 0.f, 200.f));
	}
	return Best;
}

void AJMMyPlayer::HandleCursorClick(bool bRightButton, const FVector2D& ScreenPos)
{
	UJMUIManager* UIManager = GetUIManager();
	const FPickResult Pick = (bRightButton && !bIsDead && !bTravelLocked) ? PickAtScreen(ScreenPos) : FPickResult();
	switch (Pick.Kind)
	{
	case EPickKind::Npc:
	case EPickKind::Portal:
		ClosePlayerMenu();
		StartAutoWalk(Pick.Kind, Pick.Id);
		return;
	case EPickKind::Player:
		OpenPlayerMenu(Pick.Id);
		return;
	default:
		break;
	}
	// 결과가 없는 곳(맨땅·몬스터·드랍·빈 곳)이나 좌클릭: 메뉴를 닫고, 열린 창이 없으면 커서를 숨긴다(창이 있으면 커서 유지).
	ClosePlayerMenu();
	if (UIManager && !UIManager->HasOpenWindow()) UIManager->SetCursorMode(false);
}

void AJMMyPlayer::OpenPlayerMenu(int32 PlayerId)
{
	UJMUIManager* UIManager = GetUIManager();
	if (UPlayerMenuWidget* Menu = UIManager ? Cast<UPlayerMenuWidget>(UIManager->GetWidget(WidgetNames::PlayerMenu)) : nullptr)
		Menu->OpenFor(PlayerId);
}

void AJMMyPlayer::ClosePlayerMenu()
{
	UJMUIManager* UIManager = GetUIManager();
	if (UPlayerMenuWidget* Menu = UIManager ? Cast<UPlayerMenuWidget>(UIManager->GetWidget(WidgetNames::PlayerMenu)) : nullptr)
		Menu->Close();
}

void AJMMyPlayer::SetFreeLook(bool bEnable)
{
	if (bFreeLook == bEnable) return;
	bFreeLook = bEnable;
	// 누른 순간의 카메라 방향을 이동 기준으로 고정한다. 떼면 다시 카메라 기준(Move).
	if (bEnable && GetController()) FreeLookMoveYaw = GetController()->GetControlRotation().Yaw;
}

void AJMMyPlayer::StartAutoWalk(EPickKind Kind, int32 Id)
{
	if (bIsDead || bTravelLocked || Id == 0) return;
	EndLocalAim(true);
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	AutoWalk.Kind = Kind;
	AutoWalk.Id = Id;
	AutoWalk.StartTime = Now;
	AutoWalk.CheckTime = Now;
	AutoWalk.CheckLocation = GetActorLocation();
	// 이미 도착 거리 안이면 첫 틱에 바로 도착 처리된다.
}

void AJMMyPlayer::StopAutoWalk(const TCHAR* Reason)
{
	if (!IsAutoWalking()) return;
	AutoWalk = FAutoWalk();
	DesiredInput = FVector2D::Zero();
	if (Reason)
	{
		if (UJMUIManager* UIManager = GetUIManager()) UIManager->ShowNotice(FText::FromString(Reason));
	}
}

void AJMMyPlayer::TickAutoWalk()
{
	if (bIsDead || bTravelLocked)
	{
		StopAutoWalk();
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	const UJMObjectManager* ObjectManager = GameInstance ? GameInstance->GetSubsystem<UJMObjectManager>() : nullptr;
	const UJMInventoryManager* Inventory = GameInstance ? GameInstance->GetSubsystem<UJMInventoryManager>() : nullptr;
	if (ObjectManager == nullptr || GetWorld() == nullptr)
	{
		StopAutoWalk();
		return;
	}

	// 목표 위치와 도착 거리: F 안내가 뜨는 거리보다 안쪽(NPC = 안내 거리(이용 거리 − 30cm)에서 30cm 더, 포털 = 반경 − 30cm — 서버가 경계에서 거부하지 않게)
	FVector Target = FVector::ZeroVector;
	float ArriveCm = 0.f;
	if (AutoWalk.Kind == EPickKind::Npc)
	{
		if (!ObjectManager->GetNpcLocation(AutoWalk.Id, Target))
		{
			StopAutoWalk(TEXT("대상이 보이지 않아 멈췄습니다."));
			return;
		}
		ArriveCm = FMath::Max(50.f, (Inventory ? static_cast<float>(Inventory->GetShopRangeCm()) : 300.f) - 30.f - 30.f);
	}
	else
	{
		const UJMObjectManager::FPortalEntry* Portal = ObjectManager->Portals.FindByPredicate(
			[this](const UJMObjectManager::FPortalEntry& Entry) { return Entry.Id == AutoWalk.Id; });
		if (Portal == nullptr)
		{
			StopAutoWalk(TEXT("대상이 보이지 않아 멈췄습니다."));
			return;
		}
		Target = Portal->Location;
		ArriveCm = FMath::Max(30.f, Portal->RadiusCm - 30.f);
	}

	const FVector MyLocation = GetActorLocation();
	const float Dist = FVector::Dist2D(MyLocation, Target);
	if (Dist <= ArriveCm)
	{
		const EPickKind Kind = AutoWalk.Kind;
		const int32 Id = AutoWalk.Id;
		StopAutoWalk();
		// NPC = 바로 F와 같은 동작(이미 열려 있으면 그대로), 포털·던전 입구 = 멈추고 [F] 안내만(D-274)
		if (Kind == EPickKind::Npc) InteractWithNpc(Id, false);
		else InteractCheckTimer = 0.f;
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - AutoWalk.StartTime > AutoWalkTimeoutSec)
	{
		StopAutoWalk(TEXT("자동 이동 시간이 지나 멈췄습니다."));
		return;
	}
	// 막힘: 막힘 판정 시간 동안 30cm도 못 갔으면 멈춘다(직선 이동이라 벽·물체를 돌아가지 않음)
	if (Now - AutoWalk.CheckTime >= AutoWalkStuckSec)
	{
		if (FVector::Dist2D(MyLocation, AutoWalk.CheckLocation) < 30.f)
		{
			StopAutoWalk(TEXT("길이 막혀 멈췄습니다."));
			return;
		}
		AutoWalk.CheckTime = Now;
		AutoWalk.CheckLocation = MyLocation;
	}
	if (IsMoveLocked())
	{
		DesiredInput = FVector2D::Zero();
		return;
	}
	AddMovementInput((Target - MyLocation).GetSafeNormal2D(), 1.f);
	DesiredInput = FVector2D(0.f, 1.f);   // 이동 상태(RUN)·이동 패킷 전송은 방향키 입력과 같은 경로
}

void AJMMyPlayer::UpdateHoverCursor()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC == nullptr) return;
	EMouseCursor::Type Want = EMouseCursor::Default;
	const UJMUIManager* UIManager = GetUIManager();
	float MouseX = 0.f, MouseY = 0.f;
	if (PC->bShowMouseCursor && UIManager && UIManager->IsCursorShown() && PC->GetMousePosition(MouseX, MouseY))
	{
		// 커서 모양(UI2 하드웨어 커서, D-274): NPC = 대화(GrabHand), 플레이어·포털 = 클릭(Hand), 그 밖 = 기본
		const FPickResult Pick = PickAtScreen(FVector2D(MouseX, MouseY));
		if (Pick.Kind == EPickKind::Npc) Want = EMouseCursor::GrabHand;
		else if (Pick.Kind == EPickKind::Player || Pick.Kind == EPickKind::Portal) Want = EMouseCursor::Hand;
	}
	if (PC->CurrentMouseCursor != Want) PC->CurrentMouseCursor = Want;
}

FString AJMMyPlayer::DevInteractState() const
{
	const UJMUIManager* UIManager = GetUIManager();
	const APlayerController* PC = Cast<APlayerController>(GetController());
	float MouseX = -1.f, MouseY = -1.f;
	if (PC) PC->GetMousePosition(MouseX, MouseY);
	const FPickResult Pick = PickAtScreen(FVector2D(MouseX, MouseY));
	static const TCHAR* KindNames[] = { TEXT("None"), TEXT("Npc"), TEXT("Player"), TEXT("Portal") };
	const UPlayerMenuWidget* Menu = UIManager ? Cast<UPlayerMenuWidget>(const_cast<UJMUIManager*>(UIManager)->GetWidget(WidgetNames::PlayerMenu)) : nullptr;
	// 화면 안의 고를 수 있는 대상(종류:id@화면 좌표 거리m) — 콘솔 RClick·RClickObject로 시험할 때 쓴다
	FString Candidates;
	if (const UJMObjectManager* ObjectManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMObjectManager>() : nullptr; ObjectManager && PC)
	{
		auto Add = [&](const TCHAR* Kind, int32 Id, const FVector& World)
		{
			FVector2D Screen;
			if (!PC->ProjectWorldLocationToScreen(World, Screen, true)) return;
			Candidates += FString::Printf(TEXT(" %s:%d@(%.0f,%.0f)%.0fm"), Kind, Id, Screen.X, Screen.Y, FVector::Dist(GetActorLocation(), World) / 100.f);
		};
		for (const TPair<int32, TObjectPtr<AJMCharacterBase>>& Pair : ObjectManager->Npcs)
			if (Pair.Value) Add(TEXT("Npc"), Pair.Key, Pair.Value->GetActorLocation());
		for (const TPair<int32, TObjectPtr<AJMCharacterBase>>& Pair : ObjectManager->Objects)
			if (Cast<AJMPlayer>(Pair.Value.Get()) && Pair.Value.Get() != this) Add(TEXT("Player"), Pair.Key, Pair.Value->GetActorLocation());
		for (const UJMObjectManager::FPortalEntry& Portal : ObjectManager->Portals)
			Add(TEXT("Portal"), Portal.Id, Portal.Location + FVector(0.f, 0.f, 100.f));
	}
	return FString::Printf(TEXT("cursorMode=%d windows=%d cursorShown=%d showMouse=%d cursor=%d freeLook=%d autoWalk=%s:%d menu=%d mouse=(%.0f,%.0f) underMouse=%s:%d moveDir=(%.2f,%.2f) pos=(%.0f,%.0f) onScreen=[%s ]"),
		UIManager && UIManager->IsCursorMode() ? 1 : 0, UIManager && UIManager->HasOpenWindow() ? 1 : 0, UIManager && UIManager->IsCursorShown() ? 1 : 0,
		PC && PC->bShowMouseCursor ? 1 : 0, PC ? static_cast<int32>(PC->CurrentMouseCursor.GetValue()) : -1, bFreeLook ? 1 : 0,
		KindNames[static_cast<int32>(AutoWalk.Kind)], AutoWalk.Id, Menu ? Menu->GetTargetId() : -1,
		MouseX, MouseY, KindNames[static_cast<int32>(Pick.Kind)], Pick.Id, DesiredInput.X, DesiredInput.Y,
		GetActorLocation().X, GetActorLocation().Y, *Candidates);
}
