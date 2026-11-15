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
			// 부활 창(A2.4): 숨겨 두었다가 내 캐릭터가 죽으면 UJMObjectManager::HandleDead가 띄운다. 창이라 커서가 보인다.
			// ZOrder는 크로스헤어(100)보다 위, 시스템 메시지(200)보다 아래. 크로스헤어 위젯은 화면 전체를 덮고 Visible이라
			// 같은 ZOrder로 그 아래에 깔리면 버튼 클릭을 가로챈다(2026-11-16 PIE에서 부활 버튼이 눌리지 않았음).
			UIManager->RegisterWidget(PC, WidgetNames::Revive, ReviveWidgetClass, true, 150, false);

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


