#include "UI/ReviveWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Network/NetworkManager.h"

void UReviveWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BtnRevive)
	{
		BtnRevive->OnClicked.AddUniqueDynamic(this, &UReviveWidget::OnReviveClicked);
	}
}

void UReviveWidget::StartCountdown(int32 DelayMs)
{
	ReadyTime = FPlatformTime::Seconds() + FMath::Max(DelayMs, 0) / 1000.0;
	RetryTime = 0.0;
	ShownSeconds = -1;
	Refresh();
}

void UReviveWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}

void UReviveWidget::Refresh()
{
	const double Now = FPlatformTime::Seconds();
	const int32 Remain = FMath::Max(FMath::CeilToInt32(ReadyTime - Now), 0);

	if (TxtMessage && Remain != ShownSeconds)
	{
		ShownSeconds = Remain;
		TxtMessage->SetText(Remain > 0
			? FText::FromString(FString::Printf(TEXT("쓰러졌습니다. %d초 뒤 부활할 수 있습니다."), Remain))
			: FText::FromString(TEXT("쓰러졌습니다. 부활하시겠습니까?")));
	}
	if (BtnRevive)
	{
		BtnRevive->SetIsEnabled(Remain == 0 && Now >= RetryTime);
	}
}

void UReviveWidget::OnReviveClicked()
{
	const double Now = FPlatformTime::Seconds();
	if (Now < ReadyTime || Now < RetryTime) return;

	if (UNetworkManager* NetManager = UNetworkManager::Get(this))
	{
		NetManager->SendRevive();
	}
	// 응답이 올 때까지 연타를 막는다. 응답이 없으면(패킷 유실·서버 시계 차이) RetrySec 뒤 다시 누를 수 있다.
	RetryTime = Now + RetrySec;
	Refresh();
}
