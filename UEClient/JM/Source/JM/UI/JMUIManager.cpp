// Fill out your copyright notice in the Description page of Project Settings.
#include "JMUIManager.h"
#include "GameFramework/PlayerController.h"
#include "Player/GameplayInputMode.h"

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
    else
    {
        // 오직 게임(캐릭터)만 조작하게 하고 마우스 커서를 숨깁니다.
        ApplyCombatInputMode(Controller);
    }
}