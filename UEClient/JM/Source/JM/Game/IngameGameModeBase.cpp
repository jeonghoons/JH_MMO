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
			// 인벤토리·장비창(A6.1): 숨겨 두었다가 I 키(AJMMyPlayer::OnInventoryInput)로 여닫는다. 크로스헤어(100) 위, 부활 창(150) 아래.
			if (InventoryWidgetClass) UIManager->RegisterWidget(PC, WidgetNames::Inventory, InventoryWidgetClass, true, 120, false);
			// 거래 창(A6.3a): 숨겨 두었다가 거래 상태가 오면 UJMTradeManager가 띄운다. 인벤토리(120)와 나란히 쓰므로 같은 층 바로 위.
			if (TradeWidgetClass) UIManager->RegisterWidget(PC, WidgetNames::Trade, TradeWidgetClass, true, 125, false);
			// 상점 창(L4): 숨겨 두었다가 SC_SHOP_OPEN이 오면 UJMShopManager가 띄운다. 거래 창과 같은 자리·같은 층(거래 중에는 상점을 쓸 수 없어 함께 뜨지 않는다).
			if (ShopWidgetClass) UIManager->RegisterWidget(PC, WidgetNames::Shop, ShopWidgetClass, true, 126, false);
			// 파티 창(DG2): 숨겨 두었다가 P 키(AJMMyPlayer::OnPartyInput)로 여닫고, 초대를 받으면 UJMPartyManager가 띄운다. 인벤토리(120)와 거래(125) 사이.
			if (PartyWidgetClass) UIManager->RegisterWidget(PC, WidgetNames::Party, PartyWidgetClass, true, 124, false);

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


