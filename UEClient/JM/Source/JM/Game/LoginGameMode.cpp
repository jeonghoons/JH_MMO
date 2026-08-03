#include "Game/LoginGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "Network/NetworkManager.h"

ALoginGameMode::ALoginGameMode()
{
	DefaultPawnClass = nullptr;
}

void ALoginGameMode::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (PC)
	{
		FInputModeUIOnly InputMode;
		PC->SetInputMode(InputMode);
		PC->bShowMouseCursor = true;

		if (LoginWidgetClass)
		{
			LoginWidgetInstance = CreateWidget<UUserWidget>(GetWorld(), LoginWidgetClass);
			if (LoginWidgetInstance)
			{
				LoginWidgetInstance->AddToViewport();
			}
		}

		UNetworkManager* NetManager = GetGameInstance()->GetSubsystem<UNetworkManager>();
		NetManager->ConnectToServer();
	}
	

}
