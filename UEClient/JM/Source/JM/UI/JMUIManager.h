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

class APlayerController;

UCLASS()
class JM_API UJMUIManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
    // 1. 위젯을 생성해 매니저에 등록하는 함수. 이름은 UI/WidgetNames.h 상수를 쓴다.
    //    bStartVisible이면 바로 보이게 하고(입력 모드는 건드리지 않음), 아니면 숨겨 둔다. 같은 이름이 있으면 교체한다.
    UUserWidget* RegisterWidget(APlayerController* PC, FName WidgetName, TSubclassOf<UUserWidget> WidgetClass, bool bIsWindow, int32 ZOrder = 0, bool bStartVisible = false);

    // 레벨 전환 전에 호출. 등록된 위젯을 모두 화면에서 떼고 목록을 비운다.
    void ClearWidgets();

    // 2. 위젯을 껐다 켰다 하는 함수
    void ToggleWidget(FName WidgetName);

    UUserWidget* GetWidget(FName WidgetName);

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
    TObjectPtr<APlayerController> Controller;

    
};
