#include "UI/InventoryWidget.h"
#include "UI/ItemSlotWidget.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/ScrollBox.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Data/ItemTableRow.h"
#include "Game/DataManager.h"
#include "Game/JMInventoryManager.h"
#include "Game/JMTradeManager.h"
#include "Game/JMShopManager.h"
#include "Game/JMAuctionManager.h"
#include "Network/NetworkManager.h"
#include "Components/Image.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Game/JMGameInstance.h"
#include "Game/JMObjectManager.h"
#include "Player/JMPlayer.h"
#include "Player/JMMyPlayer.h"

namespace
{
	constexpr int32 EquipPartCount = 7;   // Protocol::EquipPart 1~7

	// 빈 장착 칸에 보이는 슬롯 이름. 인덱스 = EquipPart - 1
	const TCHAR* const EquipPartLabels[EquipPartCount] = { TEXT("머리"), TEXT("상의"), TEXT("하의"), TEXT("장갑"), TEXT("신발"), TEXT("무기"), TEXT("보조") };
}

void UInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BtnClose)
	{
		BtnClose->OnClicked.AddUniqueDynamic(this, &UInventoryWidget::OnCloseClicked);
	}
	if (BtnDrop)
	{
		BtnDrop->OnClicked.AddUniqueDynamic(this, &UInventoryWidget::OnDropClicked);
	}
	BuildEquipSlots();

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UJMInventoryManager* Inventory = GameInstance->GetSubsystem<UJMInventoryManager>())
		{
			InventoryChangedHandle = Inventory->OnInventoryChanged.AddUObject(this, &UInventoryWidget::Refresh);
		}
	}
	// 창이 인벤토리 패킷보다 늦게 만들어져도 현재 값으로 시작한다.
	Refresh();
}

void UInventoryWidget::NativeDestruct()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UJMInventoryManager* Inventory = GameInstance->GetSubsystem<UJMInventoryManager>())
		{
			Inventory->OnInventoryChanged.Remove(InventoryChangedHandle);
		}
	}
	DestroyPreview();
	Super::NativeDestruct();
}

void UInventoryWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// 접힌(Collapsed) 창은 Tick이 오지 않으므로 닫혀 있는 동안은 찍지 않는다
	UpdatePreview();

	// 아이템을 끄는 중이면 가방 스크롤의 위·아래 끝에서 자동으로 스크롤한다(D-285)
	if (BagScroll && UWidgetBlueprintLibrary::IsDragDropping() && FSlateApplication::IsInitialized())
	{
		const FGeometry& Geo = BagScroll->GetCachedGeometry();
		const FVector2D Local = Geo.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
		const FVector2D Size = Geo.GetLocalSize();
		if (Local.X >= 0.f && Local.X <= Size.X && Size.Y > 0.f)
		{
			float Dir = 0.f;
			if (Local.Y >= -DragScrollEdgePx && Local.Y < DragScrollEdgePx) Dir = -1.f;
			else if (Local.Y > Size.Y - DragScrollEdgePx && Local.Y <= Size.Y + DragScrollEdgePx) Dir = 1.f;
			if (Dir != 0.f)
			{
				const float Offset = FMath::Clamp(BagScroll->GetScrollOffset() + Dir * DragScrollSpeed * InDeltaTime, 0.f, BagScroll->GetScrollOffsetOfEnd());
				BagScroll->SetScrollOffset(Offset);
			}
		}
	}
}

bool UInventoryWidget::EnsurePreview()
{
	if (PreviewActor && PreviewCapture && PreviewTarget) return true;
	UWorld* World = GetWorld();
	const UJMGameInstance* GameInst = World ? Cast<UJMGameInstance>(World->GetGameInstance()) : nullptr;
	if (GameInst == nullptr || !GameInst->PlayerClass) return false;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	AJMPlayer* Actor = World->SpawnActor<AJMPlayer>(GameInst->PlayerClass, PreviewLocation, FRotator::ZeroRotator, Params);
	if (Actor == nullptr) return false;
	// 무기별 손에 든 Idle로 보인다(등에 멘 Idle은 무기가 가려짐 — 사용자 요청 2026-11-23)
	Actor->SetAlwaysDrawn(true);
	// 서버 위치를 따라가지 않고(원격 보간은 액터 Tick) 떨어지지도 않게
	Actor->SetActorTickEnabled(false);
	Actor->SetActorEnableCollision(false);
	if (UCharacterMovementComponent* Move = Actor->GetCharacterMovement())
	{
		Move->DisableMovement();
		Move->SetComponentTickEnabled(false);
	}
	// 메인 화면에 안 보여도 포즈를 갱신(장비 메시는 몸 메시를 따라감)
	if (USkeletalMeshComponent* Body = Actor->GetMesh())
		Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Actor, TEXT("PreviewCapture"));
	Capture->SetupAttachment(Actor->GetRootComponent());
	Capture->SetRelativeLocationAndRotation(FVector(PreviewDistance, 0.f, PreviewHeight), FRotator(0.f, 180.f, 0.f));
	Capture->FOVAngle = PreviewFov;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->ShowOnlyActors.Add(Actor);   // 액터 단위 — 늦게 붙는 장비 메시도 포함
	TArray<FEngineShowFlagsSetting> Flags;
	for (const TCHAR* Flag : { TEXT("Atmosphere"), TEXT("Fog"), TEXT("Cloud") })
		Flags.Add(FEngineShowFlagsSetting{ Flag, false });
	Capture->SetShowFlagSettings(Flags);

	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this);
	Target->RenderTargetFormat = RTF_RGBA8_SRGB;
	Target->ClearColor = FLinearColor::Black;
	Target->InitAutoFormat(PreviewSize.X, PreviewSize.Y);
	Target->UpdateResourceImmediate(true);
	Capture->TextureTarget = Target;
	Capture->RegisterComponent();

	// 앞 위쪽 주광 + 뒤쪽 보조광(월드와 멀리 떨어져 있어 월드에는 닿지 않음)
	auto AddLight = [Actor](const TCHAR* Name, const FVector& Rel, float Lumens)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(Actor, Name);
		Light->SetupAttachment(Actor->GetRootComponent());
		Light->SetRelativeLocation(Rel);
		Light->SetIntensityUnits(ELightUnits::Lumens);
		Light->SetIntensity(Lumens);
		Light->SetAttenuationRadius(1000.f);
		Light->SetCastShadows(false);
		Light->RegisterComponent();
	};
	AddLight(TEXT("PreviewKey"), FVector(PreviewDistance * 0.8f, -120.f, 80.f), PreviewKeyLight);
	AddLight(TEXT("PreviewFill"), FVector(-200.f, 120.f, 60.f), PreviewFillLight);

	PreviewActor = Actor;
	PreviewCapture = Capture;
	PreviewTarget = Target;
	PreviewPlayerType = -1;
	PreviewItems.Reset();
	if (ImgCharacter)
	{
		ImgCharacter->SetBrushResourceObject(Target);
		ImgCharacter->SetDesiredSizeOverride(FVector2D(PreviewSize));
	}
	UE_LOG(LogTemp, Log, TEXT("[InvPreview] spawned %s at %s rt=%dx%d"), *Actor->GetName(), *PreviewLocation.ToString(), PreviewSize.X, PreviewSize.Y);
	return true;
}

void UInventoryWidget::UpdatePreview()
{
	if (ImgCharacter == nullptr) return;
	const UJMObjectManager* Objects = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMObjectManager>() : nullptr;
	const AJMMyPlayer* Me = Objects ? Objects->MyPlayer.Get() : nullptr;
	if (Me == nullptr || !EnsurePreview()) return;

	// 외형은 내 캐릭터의 서버 값(체형·장착 목록)을 그대로 — 장착이 바뀌면 다시 넣는다
	const Protocol::ObjectInfo& Info = Me->GetObjectInfo();
	TArray<int32> Items;
	for (int32 i = 0; i < Info.equip_items_size(); ++i) Items.Add(Info.equip_items(i));
	if (PreviewPlayerType != static_cast<int32>(Info.player_type()) || PreviewItems != Items)
	{
		Protocol::ObjectInfo Copy = Info;
		Copy.set_aim_skill_id(0);
		// 내 캐릭터의 지금 속도까지 복사되면 미리보기(액터 Tick 꺼짐)가 걷기·뛰기 자세로 굳는다 — 무기별 Idle로 고정(FP1)
		Copy.mutable_position()->set_v_x(0.f);
		Copy.mutable_position()->set_v_y(0.f);
		Copy.mutable_position()->set_v_z(0.f);
		PreviewActor->SetPlayerData(Copy);
		PreviewPlayerType = static_cast<int32>(Info.player_type());
		PreviewItems = Items;
		UE_LOG(LogTemp, Log, TEXT("[InvPreview] loadout type=%d items=%d"), PreviewPlayerType, Items.Num());
	}
	PreviewCapture->CaptureSceneDeferred();
}

void UInventoryWidget::DestroyPreview()
{
	if (PreviewActor && IsValid(PreviewActor)) PreviewActor->Destroy();
	PreviewActor = nullptr;
	PreviewCapture = nullptr;
	PreviewTarget = nullptr;
}

UItemSlotWidget* UInventoryWidget::CreateSlot()
{
	if (!ItemSlotClass) return nullptr;

	UItemSlotWidget* ItemSlot = CreateWidget<UItemSlotWidget>(this, ItemSlotClass);
	if (ItemSlot == nullptr) return nullptr;

	ItemSlot->OnClicked.BindUObject(this, &UInventoryWidget::OnSlotClicked);
	ItemSlot->OnRightClicked.BindUObject(this, &UInventoryWidget::OnSlotRightClicked);
	ItemSlot->OnDropped.BindUObject(this, &UInventoryWidget::OnSlotDropped);
	return ItemSlot;
}

void UInventoryWidget::BuildEquipSlots()
{
	if (EquipPanel == nullptr || EquipSlots.Num() > 0) return;

	for (int32 Part = 1; Part <= EquipPartCount; ++Part)
	{
		UItemSlotWidget* ItemSlot = CreateSlot();
		if (ItemSlot == nullptr) return;
		EquipPanel->AddChild(ItemSlot);
		ItemSlot->InitEquipSlot(Part, FText::FromString(EquipPartLabels[Part - 1]));
		EquipSlots.Add(ItemSlot);
	}
}

void UInventoryWidget::EnsureBagSlots(int32 SlotCount)
{
	if (BagGrid == nullptr) return;

	// 칸은 늘리기만 한다(칸 수는 서버 값이라 접속 중에는 바뀌지 않는다. 로그인 보정으로 넘친 칸이 있으면 그만큼 더 그린다).
	while (BagSlots.Num() < SlotCount)
	{
		UItemSlotWidget* ItemSlot = CreateSlot();
		if (ItemSlot == nullptr) return;
		const int32 Index = BagSlots.Num();
		BagGrid->AddChildToUniformGrid(ItemSlot, Index / Columns, Index % Columns);
		ItemSlot->InitBagSlot(Index);
		BagSlots.Add(ItemSlot);
	}
}

void UInventoryWidget::Refresh()
{
	UGameInstance* GameInstance = GetGameInstance();
	UJMInventoryManager* Inventory = GameInstance ? GameInstance->GetSubsystem<UJMInventoryManager>() : nullptr;
	UDataManager* DataManager = GameInstance ? GameInstance->GetSubsystem<UDataManager>() : nullptr;
	if (Inventory == nullptr) return;

	// 칸 번호별·장착 슬롯별로 나눈다
	TMap<int32, const FJMInventoryItem*> BagBySlot;
	const FJMInventoryItem* Equipped[EquipPartCount] = {};
	int32 MaxUsedSlot = -1;
	const FJMInventoryItem* SelectedItem = nullptr;
	for (const TPair<int64, FJMInventoryItem>& Pair : Inventory->GetItems())
	{
		const FJMInventoryItem& Item = Pair.Value;
		if (Item.EquipPart >= 1 && Item.EquipPart <= EquipPartCount)
		{
			Equipped[Item.EquipPart - 1] = &Item;
		}
		else if (Item.EquipPart == 0 && Item.Slot >= 0)
		{
			BagBySlot.Add(Item.Slot, &Item);
			MaxUsedSlot = FMath::Max(MaxUsedSlot, Item.Slot);
		}
		if (Item.ItemDbId == SelectedDbId) SelectedItem = &Item;
	}
	if (SelectedItem == nullptr) SelectedDbId = 0;   // 선택한 아이템이 사라졌다

	EnsureBagSlots(FMath::Max(Inventory->GetMaxSlots(), MaxUsedSlot + 1));

	auto FillSlot = [&](UItemSlotWidget* ItemSlot, const FJMInventoryItem* Item)
	{
		if (ItemSlot == nullptr) return;
		ItemSlot->SetItem(Item, (Item && DataManager) ? DataManager->FindItemById(Item->ItemId) : nullptr);
		ItemSlot->SetSelected(Item != nullptr && Item->ItemDbId == SelectedDbId);
	};
	for (int32 Index = 0; Index < BagSlots.Num(); ++Index)
	{
		const FJMInventoryItem* const* Found = BagBySlot.Find(Index);
		FillSlot(BagSlots[Index], Found ? *Found : nullptr);
	}
	for (int32 Index = 0; Index < EquipSlots.Num(); ++Index)
	{
		FillSlot(EquipSlots[Index], Equipped[Index]);
	}

	// 버리기는 인벤토리 칸의 아이템을 골랐을 때만 누를 수 있다.
	if (BtnDrop) BtnDrop->SetIsEnabled(SelectedItem != nullptr && SelectedItem->EquipPart == 0);

	if (TxtGold) TxtGold->SetText(FText::AsNumber(Inventory->GetGold()));
	if (TxtSlots) TxtSlots->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), BagBySlot.Num(), Inventory->GetMaxSlots())));
	if (TxtSelected)
	{
		FText Text;
		if (SelectedItem)
		{
			const FItemTableRow* Row = DataManager ? DataManager->FindItemById(SelectedItem->ItemId) : nullptr;
			const FString Name = Row ? Row->DisplayName.ToString() : FString::FromInt(SelectedItem->ItemId);
			Text = FText::FromString(SelectedItem->Count >= 2 ? FString::Printf(TEXT("%s x%lld"), *Name, SelectedItem->Count) : Name);
		}
		TxtSelected->SetText(Text);
	}
}

void UInventoryWidget::OnSlotClicked(UItemSlotWidget* ItemSlot)
{
	if (ItemSlot == nullptr) return;
	// 빈 칸을 누르면 선택을 푼다
	SelectedDbId = ItemSlot->GetItemDbId();
	Refresh();
}

void UInventoryWidget::OnSlotRightClicked(UItemSlotWidget* ItemSlot)
{
	if (ItemSlot == nullptr || !ItemSlot->HasItem()) return;
	UNetworkManager* NetManager = UNetworkManager::Get(this);
	if (NetManager == nullptr) return;

	if (ItemSlot->IsEquipSlot())
	{
		NetManager->SendChangeEquip(ItemSlot->GetEquipPart(), 0);   // 해제
		return;
	}

	// 상점이 열려 있으면 인벤토리 아이템 우클릭 = 판매 후보로 고르기(L4). 판매는 상점 창의 [판매]로 확정한다(되사기가 없어 바로 팔지 않는다).
	if (UJMShopManager* Shop = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMShopManager>() : nullptr)
	{
		if (Shop->IsOpen())
		{
			Shop->SetSellCandidate(ItemSlot->GetItemDbId());
			return;
		}
	}

	// 경매 창을 경매 NPC 앞에서 열어 두었으면 인벤토리 아이템 우클릭 = 등록 후보로 고르기(A6.3b). 수량·가격을 적고 경매 창의 [등록]으로 확정한다.
	if (UJMAuctionManager* Auction = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMAuctionManager>() : nullptr)
	{
		if (Auction->IsAtNpc())
		{
			Auction->SetRegisterCandidate(ItemSlot->GetItemDbId());
			return;
		}
	}

	// 거래의 제시 단계에서는 인벤토리 아이템 우클릭 = 내 제시에 올리기(행 전부, A6.3a). 장착·사용은 거래가 끝난 뒤에 한다.
	if (UJMTradeManager* Trade = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMTradeManager>() : nullptr)
	{
		if (Trade->IsOffering())
		{
			Trade->AddItem(ItemSlot->GetItemDbId(), 0);
			return;
		}
	}

	// 인벤토리의 장비: 그 아이템의 슬롯에 장착. 소비 아이템: 그 행에서 하나 사용(A3.6).
	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FItemTableRow* Row = DataManager ? DataManager->FindItemById(ItemSlot->GetItemId()) : nullptr;
	if (Row == nullptr) return;
	if (Row->Slot == EEquipSlot::None)
	{
		if (Row->UseEffect != EItemUseEffect::None) NetManager->SendUseItem(ItemSlot->GetItemDbId(), 0);
		return;
	}
	NetManager->SendChangeEquip(static_cast<int32>(Row->Slot), ItemSlot->GetItemDbId());
}

void UInventoryWidget::OnSlotDropped(UItemSlotWidget* From, UItemSlotWidget* To)
{
	if (From == nullptr || To == nullptr || !From->HasItem()) return;
	UNetworkManager* NetManager = UNetworkManager::Get(this);
	if (NetManager == nullptr) return;

	// 검증은 모두 서버가 한다(슬롯이 맞는지, 빈 칸이 있는지 등). 여기서는 어떤 요청인지만 고른다.
	if (!From->IsEquipSlot() && !To->IsEquipSlot())
	{
		// 인벤토리 → 인벤토리: 칸 옮기기(대상에 아이템이 있으면 서버가 맞바꾼다)
		NetManager->SendMoveItem(From->GetItemDbId(), To->GetSlotIndex());
	}
	else if (!From->IsEquipSlot() && To->IsEquipSlot())
	{
		// 인벤토리 → 장착 칸: 그 슬롯에 장착
		NetManager->SendChangeEquip(To->GetEquipPart(), From->GetItemDbId());
	}
	else if (From->IsEquipSlot() && !To->IsEquipSlot())
	{
		// 장착 칸 → 인벤토리: 대상 칸에 같은 슬롯의 장비가 있으면 그것으로 교체(옛 장비가 그 칸으로 내려온다), 아니면 해제(앞의 빈 칸으로).
		UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
		const FItemTableRow* TargetRow = (To->HasItem() && DataManager) ? DataManager->FindItemById(To->GetItemId()) : nullptr;
		const bool bSwap = TargetRow != nullptr && static_cast<int32>(TargetRow->Slot) == From->GetEquipPart();
		NetManager->SendChangeEquip(From->GetEquipPart(), bSwap ? To->GetItemDbId() : 0);
	}
}

void UInventoryWidget::OnDropClicked()
{
	if (SelectedDbId == 0) return;
	// 요청만 보낸다. 서버가 행을 지운 뒤 SC_INVENTORY_UPDATE(칸에서 사라짐)와 SC_ADD_OBJECT(발밑 드랍)를 보낸다. 거부는 시스템 메시지.
	if (UNetworkManager* NetManager = UNetworkManager::Get(this))
	{
		NetManager->SendDropItem(SelectedDbId);
	}
}

void UInventoryWidget::OnCloseClicked()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UJMUIManager* UIManager = GameInstance->GetSubsystem<UJMUIManager>())
		{
			UIManager->SetWidgetVisible(WidgetNames::Inventory, false);
		}
	}
}
