// Fill out your copyright notice in the Description page of Project Settings.
#include "JMUIManager.h"
#include "GameFramework/PlayerController.h"
#include "Player/GameplayInputMode.h"
#include "UI/SystemMessageWidget.h"
#include "UI/WidgetNames.h"
#include "Game/JMGameInstance.h"

UUserWidget* UJMUIManager::RegisterWidget(APlayerController* PC, FName WidgetName, TSubclassOf<UUserWidget> WidgetClass, bool bIsWindow, int32 ZOrder, bool bStartVisible)
{
    if (!PC || !WidgetClass) return nullptr;

    Controller = PC;

    // 같은 이름이 이미 있으면 이전 위젯을 떼어내고 교체합니다.
    if (FWidgetInfo* Old = ManagedWidgets.Find(WidgetName))
    {
        if (Old->WidgetInstance)
        {
            Old->WidgetInstance->RemoveFromParent();
        }
        ManagedWidgets.Remove(WidgetName);
        OpenedWindows.Remove(WidgetName);
    }

    // 위젯을 생성해서 변수에 담아둡니다.
    UUserWidget* NewWidget = CreateWidget<UUserWidget>(PC, WidgetClass);
    if (NewWidget)
    {
        NewWidget->AddToViewport(ZOrder);
        NewWidget->SetVisibility(bStartVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        if (bStartVisible && bIsWindow)
        {
            OpenedWindows.AddUnique(WidgetName);
        }

        // Map에 저장합니다.
        ManagedWidgets.Add(WidgetName, FWidgetInfo(NewWidget, bIsWindow));
    }
    return NewWidget;
}

void UJMUIManager::ClearWidgets()
{
    for (TPair<FName, FWidgetInfo>& Pair : ManagedWidgets)
    {
        if (Pair.Value.WidgetInstance)
        {
            Pair.Value.WidgetInstance->RemoveFromParent();
        }
    }
    ManagedWidgets.Empty();
    OpenedWindows.Empty();
    Controller = nullptr;

    // 대기 메시지는 이전 레벨의 일이므로 버리고, 확인 메시지는 새 레벨에서 다시 띄운다.
    PendingSystemMessages.RemoveAll([](const FPendingSystemMessage& Pending) { return Pending.Type == ESystemMessageType::Waiting; });
    bSystemMessageShown = false;
}

void UJMUIManager::ToggleWidget(FName WidgetName)
{
    // 이름으로 위젯을 찾습니다.
    if (!ManagedWidgets.Contains(WidgetName)) return;

    FWidgetInfo& Info = ManagedWidgets[WidgetName];
    if (!Info.WidgetInstance) return;

    // 현재 위젯이 숨겨져 있다면 -> 켭니다.
    if (Info.WidgetInstance->GetVisibility() == ESlateVisibility::Collapsed)
    {
        Info.WidgetInstance->SetVisibility(ESlateVisibility::Visible);

        // 창(Window) 종류라면 열려있는 목록에 이름을 추가합니다.
        if (Info.bIsWindow)
        {
            OpenedWindows.AddUnique(WidgetName);
        }
    }
    // 현재 위젯이 켜져 있다면 -> 끕니다.
    else
    {
        Info.WidgetInstance->SetVisibility(ESlateVisibility::Collapsed);

        if (Info.bIsWindow)
        {
            OpenedWindows.Remove(WidgetName);
        }
    }

    // 창이 켜지거나 꺼졌으므로, 마우스 커서 상태를 갱신합니다.
    UpdateInputMode();
}

UUserWidget* UJMUIManager::GetWidget(FName WidgetName)
{
    if (ManagedWidgets.Contains(WidgetName))
    {
        return ManagedWidgets[WidgetName].WidgetInstance;
    }
    return nullptr;
}


void UJMUIManager::UpdateInputMode()
{
    if (!Controller) return;

    // 열려있는 창(인벤토리, 메뉴 등)이 1개라도 있다면
    if (OpenedWindows.Num() > 0)
    {
        // UI와 게임 둘 다 조작 가능하게 하고 마우스 커서를 켭니다.
        FInputModeGameAndUI InputMode;
        InputMode.SetHideCursorDuringCapture(false); // 마우스 클릭해도 커서 유지
        Controller->SetInputMode(InputMode);
        Controller->bShowMouseCursor = true;
    }
    // 열려있는 창이 하나도 없다면 (평상시 상태)
    else if (bGameplayInput)
    {
        // 오직 게임(캐릭터)만 조작하게 하고 마우스 커서를 숨깁니다.
        ApplyCombatInputMode(Controller);
    }
    // 로그인 화면처럼 게임 입력이 없는 레벨은 UI만 조작하고 커서를 보입니다.
    else
    {
        Controller->SetInputMode(FInputModeUIOnly());
        Controller->bShowMouseCursor = true;
    }
}

void UJMUIManager::SetGameplayInput(bool bEnable)
{
    bGameplayInput = bEnable;

    // 레벨 전환 전에 쌓인 메시지가 있으면 새 레벨에서 띄운다.
    if (!bSystemMessageShown && PendingSystemMessages.Num() > 0)
    {
        ShowNextSystemMessage();
    }
}

void UJMUIManager::ShowSystemMessage(const FText& Message, ESystemMessageType Type)
{
    UE_LOG(LogTemp, Log, TEXT("System message: %s"), *Message.ToString());

    // 대기 메시지는 지금 떠 있는 메시지 바로 다음 차례로, 확인 메시지는 맨 뒤로.
    const int32 InsertIndex = (Type == ESystemMessageType::Waiting) ? (bSystemMessageShown ? 1 : 0) : PendingSystemMessages.Num();
    PendingSystemMessages.Insert(FPendingSystemMessage{ Message, Type }, FMath::Min(InsertIndex, PendingSystemMessages.Num()));

    if (!bSystemMessageShown)
    {
        ShowNextSystemMessage();
    }
}

void UJMUIManager::CloseSystemMessage()
{
    if (!bSystemMessageShown) return;

    // 떠 있는 메시지는 항상 목록의 맨 앞이다.
    if (PendingSystemMessages.Num() > 0)
    {
        PendingSystemMessages.RemoveAt(0);
    }
    bSystemMessageShown = false;

    if (PendingSystemMessages.Num() > 0)
    {
        ShowNextSystemMessage();
        return;
    }

    if (FWidgetInfo* Info = ManagedWidgets.Find(WidgetNames::SystemMessage))
    {
        if (Info->WidgetInstance)
        {
            Info->WidgetInstance->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
    OpenedWindows.Remove(WidgetNames::SystemMessage);
    UpdateInputMode();
}

void UJMUIManager::CloseWaitingMessage()
{
    const bool bShownIsWaiting = bSystemMessageShown && PendingSystemMessages.Num() > 0 && PendingSystemMessages[0].Type == ESystemMessageType::Waiting;

    // 아직 차례가 오지 않은 대기 메시지를 먼저 지운다(떠 있는 맨 앞 메시지는 남긴다).
    const int32 FirstPending = bSystemMessageShown ? 1 : 0;
    for (int32 Index = PendingSystemMessages.Num() - 1; Index >= FirstPending; --Index)
    {
        if (PendingSystemMessages[Index].Type == ESystemMessageType::Waiting)
        {
            PendingSystemMessages.RemoveAt(Index);
        }
    }

    if (bShownIsWaiting)
    {
        CloseSystemMessage();
    }
}

void UJMUIManager::ShowNextSystemMessage()
{
    if (PendingSystemMessages.Num() == 0) return;

    UUserWidget* Widget = GetWidget(WidgetNames::SystemMessage);
    if (Widget == nullptr)
    {
        UJMGameInstance* GameInstance = Cast<UJMGameInstance>(GetGameInstance());
        APlayerController* PC = GameInstance ? GameInstance->GetFirstLocalPlayerController() : nullptr;
        if (GameInstance == nullptr || GameInstance->SystemMessageWidgetClass == nullptr)
        {
            UE_LOG(LogTemp, Warning, TEXT("ShowSystemMessage: SystemMessageWidgetClass is not set in the GameInstance"));
            return;
        }
        if (PC == nullptr) return;   // 레벨이 아직 준비되지 않았다. SetGameplayInput에서 다시 시도한다.

        // 다른 UI보다 위에 둔다.
        Widget = RegisterWidget(PC, WidgetNames::SystemMessage, GameInstance->SystemMessageWidgetClass, true, 200, false);
    }

    USystemMessageWidget* MessageWidget = Cast<USystemMessageWidget>(Widget);
    if (MessageWidget == nullptr) return;

    const FPendingSystemMessage& Next = PendingSystemMessages[0];
    MessageWidget->Setup(Next.Message, Next.Type == ESystemMessageType::Waiting);
    MessageWidget->SetVisibility(ESlateVisibility::Visible);
    bSystemMessageShown = true;

    OpenedWindows.AddUnique(WidgetNames::SystemMessage);
    UpdateInputMode();
}