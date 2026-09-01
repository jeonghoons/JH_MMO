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

AJMMyPlayer::AJMMyPlayer()
{
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	IsMyPlayer = true;
}

void AJMMyPlayer::BeginPlay()
{
	Super::BeginPlay();
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
	}
}

void AJMMyPlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (GetGameInstance()->GetSubsystem<UNetworkManager>() == nullptr)
		return;

	UNetworkManager* NetManager = GetGameInstance()->GetSubsystem<UNetworkManager>();
	if (NetManager == nullptr) return;

	bool forceSendPacket = false;

	if (LastDesiredInput != DesiredInput) {
		forceSendPacket = true;
		LastDesiredInput = DesiredInput;
	}

	FVector currentVelocity = GetVelocity();

	/*if (DesiredInput == FVector2D::Zero())
		SetMoveState(Move_State::IDLE);
	else
		SetMoveState(Move_State::RUN);*/

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
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
		DesiredYaw = GetActorRotation().Yaw;
	}
}
