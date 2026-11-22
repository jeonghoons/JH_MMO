#include "Game/JMQuestManager.h"
#include "Game/DataManager.h"
#include "Game/JMInventoryManager.h"
#include "Game/JMObjectManager.h"
#include "Network/NetworkManager.h"
#include "UI/ChatWidget.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"

namespace
{
	// 거부 결과(SC_QUEST_RESULT) 문구. BUSY·TOO_FREQUENT(연타)는 띄우지 않는다.
	FText QuestResultText(Protocol::QuestResult Result)
	{
		switch (Result)
		{
		case Protocol::QUEST_RESULT_NOT_IN_ROOM:		return FText::FromString(TEXT("게임에 들어간 뒤에 할 수 있습니다."));
		case Protocol::QUEST_RESULT_DEAD:				return FText::FromString(TEXT("사망 중에는 할 수 없습니다."));
		case Protocol::QUEST_RESULT_NOT_ALLOWED:		return FText::FromString(TEXT("이 계정은 퀘스트를 할 수 없습니다."));
		case Protocol::QUEST_RESULT_NO_QUEST:			return FText::FromString(TEXT("이 NPC에게서 받을 수 없는 퀘스트입니다."));
		case Protocol::QUEST_RESULT_TOO_FAR:			return FText::FromString(TEXT("NPC에게서 너무 멀리 있습니다."));
		case Protocol::QUEST_RESULT_IN_TRADE:			return FText::FromString(TEXT("거래 중에는 할 수 없습니다."));
		case Protocol::QUEST_RESULT_ALREADY_DONE:		return FText::FromString(TEXT("이미 완료한 퀘스트입니다."));
		case Protocol::QUEST_RESULT_NOT_FINISHED:		return FText::FromString(TEXT("아직 목표를 채우지 못했습니다."));
		case Protocol::QUEST_RESULT_NOT_ENOUGH_ITEM:	return FText::FromString(TEXT("가져올 아이템이 모자랍니다."));
		case Protocol::QUEST_RESULT_INVENTORY_FULL:		return FText::FromString(TEXT("인벤토리에 빈 칸이 없어 보상을 받을 수 없습니다."));
		case Protocol::QUEST_RESULT_GOLD_FULL:			return FText::FromString(TEXT("골드를 더 가질 수 없어 보상을 받을 수 없습니다."));
		case Protocol::QUEST_RESULT_DB_ERROR:			return FText::FromString(TEXT("서버 오류로 처리하지 못했습니다."));
		default:										return FText::FromString(TEXT("퀘스트 요청을 처리하지 못했습니다."));
		}
	}

	// 창을 열 때 먼저 고를 순서: 완료할 수 있는 것 > 받을 수 있는 것 > 진행 중 > 완료한 것
	int32 OpenPriority(const UJMQuestManager& Manager, int32 QuestId)
	{
		if (Manager.IsReadyToComplete(QuestId)) return 0;
		switch (Manager.GetState(QuestId))
		{
		case Protocol::QUEST_STATE_NONE:	return 1;
		case Protocol::QUEST_STATE_ACTIVE:	return 2;
		default:							return 3;
		}
	}
}

void UJMQuestManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 수집형 진행은 인벤토리 수량이라 인벤토리가 바뀌면 창·HUD·표식을 다시 그린다.
	if (UJMInventoryManager* Inventory = Collection.InitializeDependency<UJMInventoryManager>())
	{
		InventoryChangedHandle = Inventory->OnInventoryChanged.AddUObject(this, &UJMQuestManager::OnInventoryChanged);
	}
}

void UJMQuestManager::Deinitialize()
{
	if (UJMInventoryManager* Inventory = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMInventoryManager>() : nullptr)
	{
		Inventory->OnInventoryChanged.Remove(InventoryChangedHandle);
	}
	Super::Deinitialize();
}

void UJMQuestManager::OnInventoryChanged()
{
	OnQuestChanged.Broadcast();
}

void UJMQuestManager::HandleList(const Protocol::SC_QUEST_LIST_PACKET& Pkt)
{
	Quests.Reset();
	for (const Protocol::QuestInfo& Info : Pkt.quests())
	{
		FJMQuest& Quest = Quests.Add(Info.quest_id());
		Quest.QuestId = Info.quest_id();
		Quest.State = Info.state();
		Quest.Progress = Info.progress();
	}
	UE_LOG(LogTemp, Log, TEXT("Quest list: %d quests"), Quests.Num());
	OnQuestChanged.Broadcast();
}

void UJMQuestManager::HandleUpdate(const Protocol::QuestInfo& Info)
{
	const int32 QuestId = Info.quest_id();
	const Protocol::QuestState OldState = GetState(QuestId);
	const bool bWasReady = IsReadyToComplete(QuestId);

	FJMQuest& Quest = Quests.FindOrAdd(QuestId);
	Quest.QuestId = QuestId;
	Quest.State = Info.state();
	Quest.Progress = Info.progress();
	UE_LOG(LogTemp, Log, TEXT("Quest update: quest=%d state=%d progress=%d"), QuestId, static_cast<int32>(Quest.State), Quest.Progress);

	const FString Name = QuestName(QuestId);
	if (Quest.State == Protocol::QUEST_STATE_ACTIVE && OldState == Protocol::QUEST_STATE_NONE)
	{
		Announce(FString::Printf(TEXT("'%s' 퀘스트를 받았습니다."), *Name), FString::Printf(TEXT("퀘스트 수락: %s"), *Name));
	}
	else if (Quest.State == Protocol::QUEST_STATE_COMPLETED && OldState != Protocol::QUEST_STATE_COMPLETED)
	{
		Announce(FString::Printf(TEXT("'%s' 퀘스트를 완료했습니다."), *Name), FString::Printf(TEXT("퀘스트 완료: %s"), *Name));
	}
	else if (Quest.State == Protocol::QUEST_STATE_ACTIVE && !bWasReady && IsReadyToComplete(QuestId))
	{
		// 처치형의 목표 달성(서버는 자동으로 완료하지 않는다 — NPC에게 돌아가야 한다)
		const FQuestTableRow* Row = nullptr;
		if (const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr) Row = DataManager->FindQuest(QuestId);
		const UJMObjectManager* ObjectManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMObjectManager>() : nullptr;
		FString NpcName = (Row && ObjectManager) ? ObjectManager->FindNpcNameByTemplate(Row->NpcId) : FString();
		if (NpcName.IsEmpty()) NpcName = TEXT("의뢰한 NPC");
		Announce(FString::Printf(TEXT("'%s' 목표를 채웠습니다. %s에게 돌아가세요."), *Name, *NpcName),
			FString::Printf(TEXT("%s 목표 달성 — %s에게 돌아가세요"), *Name, *NpcName));
	}
	OnQuestChanged.Broadcast();
}

void UJMQuestManager::HandleResult(int32 QuestId, Protocol::QuestResult Result)
{
	UE_LOG(LogTemp, Log, TEXT("Quest request rejected: quest=%d result=%d"), QuestId, static_cast<int32>(Result));
	if (Result == Protocol::QUEST_RESULT_BUSY || Result == Protocol::QUEST_RESULT_TOO_FREQUENT) return;
	ShowMessage(QuestResultText(Result));
}

void UJMQuestManager::Clear()
{
	Quests.Reset();
	ResetWindow();
	OnQuestChanged.Broadcast();
}

void UJMQuestManager::ResetWindow()
{
	bOpen = false;
	NpcObjectId = 0;
	SelectedQuestId = 0;
}

void UJMQuestManager::OpenForNpc(int32 InNpcObjectId)
{
	if (InNpcObjectId == 0) return;
	bOpen = true;
	NpcObjectId = InNpcObjectId;

	SelectedQuestId = 0;
	int32 BestPriority = MAX_int32;
	for (const int32 QuestId : GetWindowQuestIds())
	{
		const int32 Priority = OpenPriority(*this, QuestId);
		if (Priority < BestPriority)
		{
			BestPriority = Priority;
			SelectedQuestId = QuestId;
		}
	}
	SetWindowVisible(true);
	OnQuestChanged.Broadcast();
}

void UJMQuestManager::OpenLog()
{
	bOpen = true;
	NpcObjectId = 0;
	const TArray<int32> Ids = GetWindowQuestIds();
	SelectedQuestId = Ids.Num() > 0 ? Ids[0] : 0;
	SetWindowVisible(true);
	OnQuestChanged.Broadcast();
}

void UJMQuestManager::Close()
{
	if (!bOpen) return;
	ResetWindow();
	SetWindowVisible(false);
	OnQuestChanged.Broadcast();
}

void UJMQuestManager::SelectQuest(int32 QuestId)
{
	if (SelectedQuestId == QuestId) return;
	SelectedQuestId = QuestId;
	OnQuestChanged.Broadcast();
}

void UJMQuestManager::RequestSelected()
{
	if (!IsAtNpc() || SelectedQuestId == 0) return;
	Request(NpcObjectId, SelectedQuestId);
}

void UJMQuestManager::Request(int32 InNpcObjectId, int32 QuestId)
{
	if (InNpcObjectId == 0 || QuestId == 0) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendQuestRequest(InNpcObjectId, QuestId);
}

void UJMQuestManager::UpdateFromPlayer(const FVector& PlayerLocation, bool bDead)
{
	if (!IsAtNpc()) return;
	// 대화 모드: 죽었거나 NPC가 시야에서 사라졌거나(Room 이동 포함) 이용 거리(서버 값 SC_INVENTORY.shop_range_cm)를 벗어나면 닫는다.
	const UGameInstance* GameInstance = GetGameInstance();
	const UJMObjectManager* ObjectManager = GameInstance ? GameInstance->GetSubsystem<UJMObjectManager>() : nullptr;
	const UJMInventoryManager* Inventory = GameInstance ? GameInstance->GetSubsystem<UJMInventoryManager>() : nullptr;
	FVector NpcLocation;
	if (bDead || ObjectManager == nullptr || !ObjectManager->GetNpcLocation(NpcObjectId, NpcLocation)) { Close(); return; }
	const float Range = Inventory ? static_cast<float>(Inventory->GetShopRangeCm()) : 0.f;
	if (FVector::DistSquared2D(PlayerLocation, NpcLocation) > Range * Range) Close();
}

Protocol::QuestState UJMQuestManager::GetState(int32 QuestId) const
{
	const FJMQuest* Quest = Quests.Find(QuestId);
	return Quest ? Quest->State : Protocol::QUEST_STATE_NONE;
}

int32 UJMQuestManager::GetProgress(int32 QuestId) const
{
	const FJMQuest* Quest = Quests.Find(QuestId);
	if (Quest == nullptr) return 0;
	const UGameInstance* GameInstance = GetGameInstance();
	const UDataManager* DataManager = GameInstance ? GameInstance->GetSubsystem<UDataManager>() : nullptr;
	const FQuestTableRow* Row = DataManager ? DataManager->FindQuest(QuestId) : nullptr;
	if (Row == nullptr) return Quest->Progress;
	if (Quest->State == Protocol::QUEST_STATE_COMPLETED) return Row->Count;
	if (Row->Kind == EQuestKind::Collect)
	{
		const UJMInventoryManager* Inventory = GameInstance->GetSubsystem<UJMInventoryManager>();
		const int64 Have = Inventory ? Inventory->GetBagItemCount(Row->TargetId) : 0;
		return static_cast<int32>(FMath::Min<int64>(Have, Row->Count));
	}
	return FMath::Min(Quest->Progress, Row->Count);
}

bool UJMQuestManager::IsReadyToComplete(int32 QuestId) const
{
	if (GetState(QuestId) != Protocol::QUEST_STATE_ACTIVE) return false;
	const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FQuestTableRow* Row = DataManager ? DataManager->FindQuest(QuestId) : nullptr;
	return Row && Row->Count > 0 && GetProgress(QuestId) >= Row->Count;
}

TArray<int32> UJMQuestManager::GetWindowQuestIds() const
{
	TArray<int32> Ids;
	if (IsAtNpc())
	{
		if (const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr)
		{
			Ids = DataManager->GetQuestIdsByNpc(GetNpcTemplateId());
		}
		return Ids;
	}
	// 목록 모드: 진행 중 먼저, 그다음 완료(각각 ID 순)
	Ids = GetActiveQuestIds();
	TArray<int32> Done;
	for (const TPair<int32, FJMQuest>& Pair : Quests)
	{
		if (Pair.Value.State == Protocol::QUEST_STATE_COMPLETED) Done.Add(Pair.Key);
	}
	Done.Sort();
	Ids.Append(Done);
	return Ids;
}

TArray<int32> UJMQuestManager::GetActiveQuestIds() const
{
	TArray<int32> Ids;
	for (const TPair<int32, FJMQuest>& Pair : Quests)
	{
		if (Pair.Value.State == Protocol::QUEST_STATE_ACTIVE) Ids.Add(Pair.Key);
	}
	Ids.Sort();
	return Ids;
}

FString UJMQuestManager::GetNpcMarker(int32 NpcTemplateId) const
{
	const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	if (DataManager == nullptr) return FString();
	bool bCanAccept = false;
	for (const int32 QuestId : DataManager->GetQuestIdsByNpc(NpcTemplateId))
	{
		if (IsReadyToComplete(QuestId)) return TEXT("?");
		if (GetState(QuestId) == Protocol::QUEST_STATE_NONE) bCanAccept = true;
	}
	return bCanAccept ? FString(TEXT("!")) : FString();
}

int32 UJMQuestManager::GetNpcTemplateId() const
{
	const UJMObjectManager* ObjectManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMObjectManager>() : nullptr;
	return ObjectManager ? ObjectManager->GetNpcTemplateId(NpcObjectId) : 0;
}

FString UJMQuestManager::QuestName(int32 QuestId) const
{
	const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FQuestTableRow* Row = DataManager ? DataManager->FindQuest(QuestId) : nullptr;
	return Row ? Row->DisplayName : FString::Printf(TEXT("퀘스트 %d"), QuestId);
}

void UJMQuestManager::SetWindowVisible(bool bVisible)
{
	if (UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr)
	{
		UIManager->SetWidgetVisible(WidgetNames::Quest, bVisible);
	}
}

void UJMQuestManager::ShowMessage(const FText& Message)
{
	if (UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr)
	{
		UIManager->ShowSystemMessage(Message);
	}
}

void UJMQuestManager::Announce(const FString& Message, const FString& Notice)
{
	UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr;
	// 화면 위쪽에 잠깐 뜨는 알림 줄(D-266) + 기록으로 남는 채팅 한 줄
	if (UIManager) UIManager->ShowNotice(FText::FromString(Notice));
	if (UChatWidget* ChatWidget = UIManager ? Cast<UChatWidget>(UIManager->GetWidget(WidgetNames::Chatting)) : nullptr)
	{
		ChatWidget->AddChatMessage(TEXT("퀘스트"), Message);
	}
}
