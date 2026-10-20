#include "Game/IngameGameModeBase.h"
#include "Player/JMPlayerController.h"
#include "Player/GameplayInputMode.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "Network/NetworkManager.h"

AIngameGameModeBase::AIngameGameModeBase()
{
	DefaultPawnClass = nullptr;
}

void AIngameGameModeBase::BeginPlay()
{
	Super::BeginPlay();
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (PC)
	{
		ApplyCombatInputMode(PC);
	}

	if (HUDWidgetClass)
	{
		HUDWidgetInstance = CreateWidget<UUserWidget>(GetWorld(), HUDWidgetClass);
		if (HUDWidgetInstance)
		{
			HUDWidgetInstance->AddToViewport();
		}
	}


	UNetworkManager* NetManager = GetGameInstance()->GetSubsystem<UNetworkManager>();
	if (NetManager)
	{
		// NetManager->OnInGameLevelLoaded();
	}
}


