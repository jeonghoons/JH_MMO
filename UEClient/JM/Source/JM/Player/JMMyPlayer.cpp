#include "Player/JMMyPlayer.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "JMPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Network/NetworkManager.h"
#include "Protocol/Protocol.pb.h"
#include "Network/SendBuffer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Game/DataManager.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Data/SkillTableRow.h"
#include "Player/NpcCharaceter.h"

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
}

void AJMMyPlayer::BeginPlay()
{
	Super::BeginPlay();
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

		//// Jumping
		//EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		//EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AJMMyPlayer::Move);
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &AJMMyPlayer::Move);

		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AJMMyPlayer::Look);
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AJMMyPlayer::Look);
		if (AttackAction) EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Started, this, &AJMMyPlayer::OnAttackInput);
		if (Skill1Action) EnhancedInputComponent->BindAction(Skill1Action, ETriggerEvent::Started, this, &AJMMyPlayer::OnSkill1Input);
		if (CancelAimAction) EnhancedInputComponent->BindAction(CancelAimAction, ETriggerEvent::Started, this, &AJMMyPlayer::OnCancelAimInput);
		
	}
}

void AJMMyPlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bAiming)
	{
		const FSkillTableRow* AimSkill = FindMySkill(AimingSlot);
		FAimResult Aim;
		if (AimSkill && ComputeAim(*AimSkill, Aim)) DrawAimIndicator(*AimSkill, Aim);
		else if (AimSkill == nullptr) bAiming = false;
	}

	/*if (GetGameInstance()->GetSubsystem<UNetworkManager>() == nullptr)
		return;*/

	UNetworkManager* NetManager = GetGameInstance()->GetSubsystem<UNetworkManager>();
	if (NetManager == nullptr) return;

	bool forceSendPacket = false;

	if (LastDesiredInput != DesiredInput) {
		forceSendPacket = true;
		LastDesiredInput = DesiredInput;
	}

	FVector currentVelocity = GetVelocity();

	if (DesiredInput == FVector2D::Zero())
		ObjectInfo.mutable_position()->set_state(Protocol::MOVE_STATE_IDLE);
	else
		ObjectInfo.mutable_position()->set_state(Protocol::MOVE_STATE_RUN);

	MovePacketSendTimer -= DeltaTime;

	if (MovePacketSendTimer <= 0 || forceSendPacket)
	{
		MovePacketSendTimer = MOVE_PACKET_SEND_DELAY;

		Protocol::CS_MOVE_PACKET movePkt;
		movePkt.mutable_pos_info()->CopyFrom(ObjectInfo.position());
		movePkt.set_force(forceSendPacket);
		TSharedPtr<SendBuffer> sendBuffer = SendBuffer::MakeSendBuffer(movePkt, Protocol::CS_MOVE);
		NetManager->SendPacket(sendBuffer);
	}

}

void AJMMyPlayer::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (GetController() != nullptr)
	{
		// 카메라(컨트롤러) 방향 기준 이동: W = 카메라가 바라보는 쪽, D = 카메라 오른쪽.
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

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
	
	if (GetController() != nullptr)
	{
		AddControllerYawInput(LookAxisVector.X * LookSensitivityYaw);
		AddControllerPitchInput(LookAxisVector.Y * LookSensitivityPitch);
		DesiredYaw = GetActorRotation().Yaw;
	}
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

	return DataManager->FindSkillBySlot(static_cast<int32>(ObjectInfo.player_type()), Slot);
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

		const ANpcCharaceter* Best = nullptr;
		float BestDistSq = TNumericLimits<float>::Max();
		for (const FHitResult& Hit : Hits)
		{
			const ANpcCharaceter* Npc = Cast<ANpcCharaceter>(Hit.GetActor());
			if (Npc == nullptr || Npc->IsDeadActor()) continue;

			const float DistSq = FVector::DistSquaredXY(Self, Npc->GetActorLocation());
			if (DistSq > FMath::Square(Skill.CastRange)) continue;   // 사거리 밖
			if (DistSq < BestDistSq) { BestDistSq = DistSq; Best = Npc; }
		}
		if (Best == nullptr) return false;   // 대상이 없으면 아무 일도 하지 않는다.

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

void AJMMyPlayer::DrawAimIndicator(const FSkillTableRow& Skill, const FAimResult& Aim) const
{
	// 임시 표시: 디버그 도형. 장판/부채꼴 데칼 연출은 A2.2d에서 교체한다.
	const float FeetZ = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f;
	const FVector Self(GetActorLocation().X, GetActorLocation().Y, FeetZ);
	const FColor Color = FColor(255, 80, 80);

	if (Skill.HitShape == ESkillHitShape::Zone)
	{
		const FVector Center(Aim.Point.X, Aim.Point.Y, FeetZ);
		DrawDebugCircle(GetWorld(), Center, Skill.Radius, 48, Color, false, -1.f, 0, 4.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
		DrawDebugLine(GetWorld(), Self, Center, Color, false, -1.f, 0, 2.f);
		DrawDebugCircle(GetWorld(), Self, Skill.CastRange, 64, FColor(120, 120, 120), false, -1.f, 0, 1.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
	}
	else if (Skill.HitShape == ESkillHitShape::Sector)
	{
		const FVector Dir = FRotator(0.f, Aim.Yaw, 0.f).Vector();
		const float HalfAngle = FMath::DegreesToRadians(Skill.Angle * 0.5f);
		DrawDebugCircleArc(GetWorld(), Self, Skill.Radius, Dir, HalfAngle, 32, Color, false, -1.f, 0, 4.f);
		DrawDebugLine(GetWorld(), Self, Self + FVector(Dir.X * FMath::Cos(HalfAngle) - Dir.Y * FMath::Sin(HalfAngle), Dir.X * FMath::Sin(HalfAngle) + Dir.Y * FMath::Cos(HalfAngle), 0.f) * Skill.Radius, Color, false, -1.f, 0, 3.f);
		DrawDebugLine(GetWorld(), Self, Self + FVector(Dir.X * FMath::Cos(-HalfAngle) - Dir.Y * FMath::Sin(-HalfAngle), Dir.X * FMath::Sin(-HalfAngle) + Dir.Y * FMath::Cos(-HalfAngle), 0.f) * Skill.Radius, Color, false, -1.f, 0, 3.f);
	}
	else
	{
		const FVector Dir = FRotator(0.f, Aim.Yaw, 0.f).Vector();
		DrawDebugLine(GetWorld(), Self, Self + Dir * FMath::Max(Skill.Range, Skill.CastRange), Color, false, -1.f, 0, 4.f);
	}
}

void AJMMyPlayer::CastSkillSlot(int32 Slot)
{
	const FSkillTableRow* Skill = FindMySkill(Slot);
	if (Skill == nullptr)
	{
		bAiming = false;
		return;
	}

	FAimResult Aim;
	if (!ComputeAim(*Skill, Aim)) return;   // 대상 없음: 조준 중이면 조준을 유지한다.

	if (UNetworkManager* NetManager = GetGameInstance()->GetSubsystem<UNetworkManager>())
	{
		Protocol::CS_ATTACK_PACKET AttackPkt;
		AttackPkt.set_skill_slot(Slot);
		AttackPkt.set_target_id(Aim.TargetId);
		AttackPkt.set_aim_yaw(Aim.Yaw);
		AttackPkt.set_aim_x(Aim.Point.X);
		AttackPkt.set_aim_y(Aim.Point.Y);
		TSharedPtr<SendBuffer> SendBuf = SendBuffer::MakeSendBuffer(AttackPkt, Protocol::CS_ATTACK);
		NetManager->SendPacket(SendBuf);
	}

	// 반응성을 위해 시전 방향으로 바로 돌린다. 실제 판정 방향은 서버가 확정한다.
	SetActorRotation(FRotator(0.f, Aim.Yaw, 0.f));
	LockFacingForSkill(*Skill);
	bAiming = false;
}

void AJMMyPlayer::OnAttackInput()
{
	// BP 노드와 IA_Attack이 같은 프레임에 겹쳐 들어와도 한 번만 처리한다.
	if (LastAttackInputFrame == GFrameCounter) return;
	LastAttackInputFrame = GFrameCounter;

	CastSkillSlot(bAiming ? AimingSlot : 0);
}

void AJMMyPlayer::OnSkill1Input()
{
	// 1번 키: 조준 진입. 조준 중에 다시 누르면 취소한다.
	if (bAiming && AimingSlot == 1)
	{
		bAiming = false;
		return;
	}

	if (FindMySkill(1) != nullptr)
	{
		bAiming = true;
		AimingSlot = 1;
	}
}

void AJMMyPlayer::OnCancelAimInput()
{
	bAiming = false;
}