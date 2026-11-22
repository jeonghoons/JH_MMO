#include "UI/PlayerMenuWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "Game/JMInventoryManager.h"
#include "Game/JMObjectManager.h"
#include "Game/JMPartyManager.h"
#include "Game/JMTradeManager.h"
#include "Player/JMPlayer.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"

void UPlayerMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (BtnTrade) BtnTrade->OnClicked.AddDynamic(this, &UPlayerMenuWidget::OnTradeClicked);
	if (BtnParty) BtnParty->OnClicked.AddDynamic(this, &UPlayerMenuWidget::OnPartyClicked);
	if (BtnClose) BtnClose->OnClicked.AddDynamic(this, &UPlayerMenuWidget::OnCloseClicked);
}

const AJMPlayer* UPlayerMenuWidget::FindTarget() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UJMObjectManager* ObjectManager = GameInstance ? GameInstance->GetSubsystem<UJMObjectManager>() : nullptr;
	if (ObjectManager == nullptr || TargetId == 0) return nullptr;
	const TObjectPtr<AJMCharacterBase>* Found = ObjectManager->Objects.Find(TargetId);
	return Found ? Cast<AJMPlayer>(Found->Get()) : nullptr;
}

void UPlayerMenuWidget::OpenFor(int32 PlayerId)
{
	TargetId = PlayerId;
	const AJMPlayer* Target = FindTarget();
	if (Target == nullptr)
	{
		TargetId = 0;
		return;
	}
	if (TxtTitleName) TxtTitleName->SetText(FText::FromString(FString(UTF8_TO_TCHAR(Target->GetObjectInfo().name().c_str()))));
	UpdatePosition();
	if (UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr)
		UIManager->SetWidgetVisible(WidgetNames::PlayerMenu, true);
}

void UPlayerMenuWidget::Close()
{
	if (TargetId == 0 && GetVisibility() == ESlateVisibility::Collapsed) return;
	TargetId = 0;
	if (UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr)
		UIManager->SetWidgetVisible(WidgetNames::PlayerMenu, false);
}

bool UPlayerMenuWidget::UpdatePosition()
{
	const AJMPlayer* Target = FindTarget();
	APlayerController* PC = GetOwningPlayer();
	if (Target == nullptr || PC == nullptr) return false;

	const UCapsuleComponent* Capsule = Target->GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.f;
	const FVector Head = Target->GetActorLocation() + FVector(0.f, 0.f, HalfHeight + HeadOffsetCm);
	FVector2D WidgetPos;
	// 위젯 좌표(DPI 배율 적용 뒤, 뷰포트 기준)로 투영한다. 카메라 뒤면 false
	if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Head, WidgetPos, false)) return false;
	if (UCanvasPanelSlot* CanvasSlot = Panel ? Cast<UCanvasPanelSlot>(Panel->Slot) : nullptr)
		CanvasSlot->SetPosition(WidgetPos + ScreenOffset);
	return true;
}

void UPlayerMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (TargetId == 0) return;

	// 대상이 사라졌거나(시야 밖·Room 이동) 죽었거나 거래 범위의 2배보다 멀어지면 닫는다.
	const AJMPlayer* Target = FindTarget();
	const APawn* MyPawn = GetOwningPlayerPawn();
	const UJMInventoryManager* Inventory = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMInventoryManager>() : nullptr;
	const float TradeRange = Inventory ? static_cast<float>(Inventory->GetTradeRangeCm()) : 0.f;
	const float MaxDist = TradeRange > 0.f ? TradeRange * 2.f : 1000.f;
	if (Target == nullptr || Target->IsDeadActor() || MyPawn == nullptr ||
		FVector::Dist2D(MyPawn->GetActorLocation(), Target->GetActorLocation()) > MaxDist || !UpdatePosition())
	{
		Close();
	}
}

void UPlayerMenuWidget::OnTradeClicked()
{
	// 거리·사망·거래 중 여부는 서버가 판정하고 거부 사유를 시스템 메시지로 알려 준다(A6.3a).
	if (UJMTradeManager* Trade = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMTradeManager>() : nullptr)
	{
		if (TargetId != 0 && !Trade->IsActive()) Trade->RequestTrade(TargetId);
	}
	Close();
}

void UPlayerMenuWidget::OnPartyClicked()
{
	// 채널·파티장·정원 검증은 서버가 한다(DG2).
	if (UJMPartyManager* Party = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMPartyManager>() : nullptr)
	{
		if (TargetId != 0) Party->Invite(TargetId);
	}
	Close();
}

void UPlayerMenuWidget::OnCloseClicked()
{
	Close();
}
