#include "Game/LoginGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "Network/NetworkManager.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"

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

		if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>())
		{
			UIManager->RegisterWidget(PC, WidgetNames::Login, LoginWidgetClass, false, 0, true);
		}

		UNetworkManager* NetManager = GetGameInstance()->GetSubsystem<UNetworkManager>();
		NetManager->ConnectToServer();
	}
	

}
