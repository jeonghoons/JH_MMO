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
		if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>())
		{
			UIManager->RegisterWidget(PC, WidgetNames::HUD, HUDWidgetClass, false, 0, true);

			// 전투 입력 모드. 시스템 메시지 창(방 입장 대기 등)이 떠 있으면 커서를 유지한다.
			UIManager->SetGameplayInput(true);
			UIManager->UpdateInputMode();
		}
		else
		{
			ApplyCombatInputMode(PC);
		}
	}
}


