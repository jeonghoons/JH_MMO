#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "JMUIManager.generated.h"

USTRUCT(BlueprintType)
struct FWidgetInfo
{
    GENERATED_BODY()

    // 실제 화면에 생성된 위젯 객체
    UPROPERTY()
    UUserWidget* WidgetInstance;

    UPROPERTY()
    bool bIsWindow;

    FWidgetInfo() : WidgetInstance(nullptr), bIsWindow(false) {}
    FWidgetInfo(UUserWidget* InWidget, bool InIsWindow) : WidgetInstance(InWidget), bIsWindow(InIsWindow) {}
};

class AJMPlayerController;

UCLASS()
class JM_API UJMUIManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
    // 1. 위젯을 매니저에 등록하고 생성하는 함수
    void RegisterWidget(AJMPlayerController* PC, FName WidgetName, TSubclassOf<UUserWidget> WidgetClass, bool bIsWindow);

    // 2. 위젯을 껐다 켰다 하는 함수
    void ToggleWidget(FName WidgetName);

    UUserWidget* GetWidget(FName WidgetName);
protected:
    // 마우스 커서 등 입력을 제어하는 내부 함수
    void UpdateInputMode();

private:
    // 모든 위젯을 이름으로 보관하는 사전(Dictionary)
    UPROPERTY()
    TMap<FName, FWidgetInfo> ManagedWidgets;

    // 현재 화면에 켜져 있는 '창(Window)'들의 이름을 담아두는 스택
    TArray<FName> OpenedWindows;

    // 입력을 제어하기 위해 플레이어 컨트롤러를 기억해 둡니다.
    UPROPERTY()
    AJMPlayerController* Controller;

    
};
