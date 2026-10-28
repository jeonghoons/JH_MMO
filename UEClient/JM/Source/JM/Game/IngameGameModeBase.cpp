#include "Game/IngameGameModeBase.h"
#include "Player/JMPlayerController.h"
#include "Player/GameplayInputMode.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"

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

		if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>())
		{
			UIManager->RegisterWidget(PC, WidgetNames::HUD, HUDWidgetClass, false, 0, true);
		}
	}
}


