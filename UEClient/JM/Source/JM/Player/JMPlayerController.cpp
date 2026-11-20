#include "Player/JMPlayerController.h"
#include "Network/NetMoveStats.h"
#include "Player/JMMyPlayer.h"
#include "Network/NetworkManager.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"
#include "Blueprint/UserWidget.h"
#include "Player/GameplayInputMode.h"
#include "UI/ChatWidget.h"
#include "Game/DataManager.h"
#include "Game/JMInventoryManager.h"
#include "Game/JMObjectManager.h"
#include "Game/JMShopManager.h"
#include "Game/JMPartyManager.h"

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
		// Room 이동으로 레벨이 바뀐 것이면(던전 입장·퇴장, DG3c-2) 문구를 달리한다.
		const UJMObjectManager* ObjectManager = GetGameInstance()->GetSubsystem<UJMObjectManager>();
		const bool bRoomTravel = ObjectManager && ObjectManager->IsRoomTravelActive();
		UIManager->ShowSystemMessage(FText::FromString(bRoomTravel ? TEXT("이동하는 중...") : TEXT("방에 입장하는 중...")), ESystemMessageType::Waiting);
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

// ---- 개발용 상점 명령(L4): 상점 창이 보내는 것과 같은 요청을 콘솔로 보낸다(마우스 없이 확인할 때). 검증은 모두 서버가 한다. ----
// Portal [id] (DG3b): id를 주면 그 포털, 생략(0)하면 범위 안의 가장 가까운 포털로 사용 요청을 보낸다.
void AJMPlayerController::Portal(int32 PortalId)
{
	UGameInstance* GameInstance = GetGameInstance();
	const UJMObjectManager* ObjectManager = GameInstance ? GameInstance->GetSubsystem<UJMObjectManager>() : nullptr;
	const APawn* MyPawn = GetPawn();
	if (ObjectManager == nullptr || MyPawn == nullptr) return;

	if (PortalId == 0)
	{
		float DistSq = 0.f;
		PortalId = ObjectManager->FindNearestPortal(MyPawn->GetActorLocation(), DistSq);
		if (PortalId == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Portal: 범위 안에 포털이 없습니다."));
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("Portal: nearest id=%d dist=%.0fcm"), PortalId, FMath::Sqrt(DistSq));
	}
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendPortal(PortalId);
}

void AJMPlayerController::ShopOpen()
{
	UGameInstance* GameInstance = GetGameInstance();
	const UJMObjectManager* ObjectManager = GameInstance ? GameInstance->GetSubsystem<UJMObjectManager>() : nullptr;
	UJMShopManager* Shop = GameInstance ? GameInstance->GetSubsystem<UJMShopManager>() : nullptr;
	const APawn* MyPawn = GetPawn();
	if (ObjectManager == nullptr || Shop == nullptr || MyPawn == nullptr) return;

	// 시야 안의 가장 가까운 NPC(거리 제한 없이 고른다 — 멀면 서버가 TOO_FAR로 거부한다).
	float DistSq = 0.f;
	const int32 NpcId = ObjectManager->FindNearestNpc(MyPawn->GetActorLocation(), 1000000.f, DistSq);
	if (NpcId == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopOpen: 시야 안에 NPC가 없습니다."));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("ShopOpen: npc=%d dist=%.0fcm"), NpcId, FMath::Sqrt(DistSq));
	Shop->RequestOpen(NpcId);
}

void AJMPlayerController::ShopBuy(int32 ItemId, int32 Count)
{
	UJMShopManager* Shop = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMShopManager>() : nullptr;
	if (Shop == nullptr) return;
	if (!Shop->IsOpen())
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopBuy: 상점이 열려 있지 않습니다. 먼저 ShopOpen"));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("ShopBuy: item=%d count=%d"), ItemId, Count);
	Shop->Buy(ItemId, Count);
}

void AJMPlayerController::ShopSell(int32 ItemId, int32 Count)
{
	UGameInstance* GameInstance = GetGameInstance();
	UJMShopManager* Shop = GameInstance ? GameInstance->GetSubsystem<UJMShopManager>() : nullptr;
	const UJMInventoryManager* Inventory = GameInstance ? GameInstance->GetSubsystem<UJMInventoryManager>() : nullptr;
	if (Shop == nullptr || Inventory == nullptr) return;
	if (!Shop->IsOpen())
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopSell: 상점이 열려 있지 않습니다. 먼저 ShopOpen"));
		return;
	}
	const int64 ItemDbId = Inventory->FindBagItemDbId(ItemId);
	if (ItemDbId == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopSell: 인벤토리에 item=%d 이(가) 없습니다."), ItemId);
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("ShopSell: item=%d dbId=%lld count=%d"), ItemId, ItemDbId, Count);
	// 창에서 하는 것과 같은 순서: 판매 후보로 고른 뒤 판다.
	Shop->SetSellCandidate(ItemDbId);
	Shop->SellSelected(Count);
}

void AJMPlayerController::ShopClose()
{
	if (UJMShopManager* Shop = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMShopManager>() : nullptr) Shop->Close();
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
// ---- 개발용 파티 명령(DG2): 파티 창·G 키·채팅 명령이 보내는 것과 같은 요청을 콘솔로 보낸다. 검증은 모두 서버가 한다. ----
void AJMPlayerController::PartyInvite(const FString& Name)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (UJMPartyManager* Party = GameInstance ? GameInstance->GetSubsystem<UJMPartyManager>() : nullptr)
	{
		Party->InviteByName(Name);
		UE_LOG(LogTemp, Log, TEXT("PartyInvite: %s"), *Name);
	}
}

void AJMPlayerController::PartyRespond(int32 Accept)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (UJMPartyManager* Party = GameInstance ? GameInstance->GetSubsystem<UJMPartyManager>() : nullptr)
	{
		UE_LOG(LogTemp, Log, TEXT("PartyRespond: accept=%d hasInvite=%d"), Accept, Party->HasInvite() ? 1 : 0);
		Party->Respond(Accept != 0);
	}
}

void AJMPlayerController::PartyLeave()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (UJMPartyManager* Party = GameInstance ? GameInstance->GetSubsystem<UJMPartyManager>() : nullptr)
	{
		Party->Leave();
	}
}

void AJMPlayerController::PartyShow()
{
	UGameInstance* GameInstance = GetGameInstance();
	const UJMPartyManager* Party = GameInstance ? GameInstance->GetSubsystem<UJMPartyManager>() : nullptr;
	if (Party == nullptr) return;

	FString Line = FString::Printf(TEXT("PartyShow: me=%d leader=%d members=%d invite=%s"), Party->GetMyId(), Party->GetLeaderId(), Party->GetMembers().Num(),
		Party->HasInvite() ? *Party->GetInviteFromName() : TEXT("-"));
	for (const FJMPartyMember& Member : Party->GetMembers())
	{
		Line += FString::Printf(TEXT(" [%d %s Lv.%d kind=%d]"), Member.ObjectId, *Member.Name, Member.Level, static_cast<int32>(Member.Kind));
	}
	UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
}

void AJMPlayerController::AutoAttack(int32 Count, int32 Slot)
{
	// 근소한 거부 측정(CP1, D-213): 시전할 수 있게 되는 첫 프레임마다 시전한다.
	if (AJMMyPlayer* MyPlayer = Cast<AJMMyPlayer>(GetPawn()))
	{
		MyPlayer->DevAutoAttack(Count, Slot);
		UE_LOG(LogTemp, Log, TEXT("AutoAttack: count=%d slot=%d"), Count, Slot);
	}
}

void AJMPlayerController::AttackStats(int32 Reset)
{
	if (AJMMyPlayer* MyPlayer = Cast<AJMMyPlayer>(GetPawn()))
		UE_LOG(LogTemp, Log, TEXT("%s"), *MyPlayer->DevAttackStats(Reset != 0));
}
