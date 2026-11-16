#include "Player/JMPlayerController.h"
#include "Network/NetMoveStats.h"
#include "Network/NetworkManager.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"
#include "Blueprint/UserWidget.h"
#include "Player/GameplayInputMode.h"
#include "UI/ChatWidget.h"
#include "Game/DataManager.h"
#include "Game/JMInventoryManager.h"

void AJMPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplyCombatInputMode(this);

	UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();
	if (UIManager)
	{
		// 블루프린트에서 할당해 둔 클래스들을 이름표와 함께 던져줍니다. 시작부터 보일 UI는 bStartVisible = true.
		// 크로스헤어는 화면 중앙 조준 기준이라 다른 UI보다 위(Z-Order 100)에 둡니다.
		UIManager->RegisterWidget(this, WidgetNames::Crosshair, CrosshairWidgetClass, false, 100, true);
		UIManager->RegisterWidget(this, WidgetNames::Chatting, ChatWidgetClass, false, 0, true);

		// 내 캐릭터가 스폰되면 UJMObjectManager::HandleSpawn이 닫는다.
		UIManager->SetGameplayInput(true);
		UIManager->ShowSystemMessage(FText::FromString(TEXT("방에 입장하는 중...")), ESystemMessageType::Waiting);
	}

	// 영웅·스킬 에셋을 먼저 로드한 뒤 입장한다. 스폰된 캐릭터가 메시 없이(투명하게) 보이지 않게 하기 위함이다.
	TWeakObjectPtr<AJMPlayerController> WeakThis(this);
	auto SendEnterRoom = [WeakThis]()
	{
		if (UNetworkManager* NetManager = WeakThis.IsValid() ? UNetworkManager::Get(WeakThis.Get()) : nullptr)
		{
			NetManager->SendEnterRoom();
		}
	};

	if (UDataManager* DataManager = GetGameInstance()->GetSubsystem<UDataManager>())
	{
		DataManager->PreloadGameAssets(FSimpleDelegate::CreateLambda(SendEnterRoom));
	}
	else
	{
		SendEnterRoom();
	}
}

void AJMPlayerController::Equip(int32 Part, int32 ItemId)
{
	// 요청만 보낸다. 슬롯·보유 검증과 결과(성공 방송·거부 문구)는 서버가 정한다.
	// 장착은 아이템 종류가 아니라 내 인벤토리의 아이템(item_db_id)으로 요청하므로(A3.3) 그 종류를 인벤토리에서 찾는다.
	UGameInstance* GameInstance = GetGameInstance();
	UNetworkManager* NetManager = GameInstance ? GameInstance->GetSubsystem<UNetworkManager>() : nullptr;
	UJMInventoryManager* Inventory = GameInstance ? GameInstance->GetSubsystem<UJMInventoryManager>() : nullptr;
	if (NetManager == nullptr || Inventory == nullptr) return;

	int64 ItemDbId = 0;
	if (ItemId != 0)
	{
		ItemDbId = Inventory->FindBagItemDbId(ItemId);
		if (ItemDbId == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Equip: 인벤토리에 item=%d 이(가) 없습니다. 먼저 Give %d 1 로 받으세요."), ItemId, ItemId);
			return;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("Equip: part=%d item=%d dbId=%lld"), Part, ItemId, ItemDbId);
	NetManager->SendChangeEquip(Part, ItemDbId);
}

void AJMPlayerController::Give(int32 ItemId, int32 Count)
{
	// 요청만 보낸다. 서버가 [Debug] DevCommands를 켜지 않았으면 거부 문구가 온다.
	if (UNetworkManager* NetManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UNetworkManager>() : nullptr)
	{
		UE_LOG(LogTemp, Log, TEXT("Give: item=%d count=%d"), ItemId, Count);
		NetManager->SendDevGive(ItemId, Count);
	}
}

void AJMPlayerController::NetMoveStats(int32 Reset)
{
	// 원격 플레이어 이동 표시 측정 결과(MC4.1b). 로그로만 남긴다.
	UE_LOG(LogTemp, Log, TEXT("%s"), *FNetMoveStats::Get().Report(Reset != 0));
}

void AJMPlayerController::OnChatKeyPressed()
{
	UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();
	if (UIManager)
	{
		// 매니저에서 채팅 위젯을 꺼내온 뒤, 우리가 만든 UCChatWidget으로 형변환(Cast)합니다.
		if (UChatWidget* ChatWidget = Cast<UChatWidget>(UIManager->GetWidget(WidgetNames::Chatting)))
		{
			// 포커스를 주는 함수를 실행합니다.
			ChatWidget->FocusChatInput(this);
		}
	}
}

void AJMPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::LeftAlt, IE_Pressed, this, &AJMPlayerController::OnFreeCursorPressed);
		InputComponent->BindKey(EKeys::LeftAlt, IE_Released, this, &AJMPlayerController::OnFreeCursorReleased);
	}
}

void AJMPlayerController::OnFreeCursorPressed()
{
	ApplyFreeCursorInputMode(this);
}

void AJMPlayerController::OnFreeCursorReleased()
{
	// If a window (inventory etc.) is open the UI manager keeps the cursor; otherwise it returns to combat mode.
	if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>())
	{
		UIManager->UpdateInputMode();
	}
	else
	{
		ApplyCombatInputMode(this);
	}
}