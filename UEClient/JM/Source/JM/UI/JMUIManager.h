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

// Notice = 확인 버튼으로 닫음, Waiting = 버튼 없이 코드가 CloseSystemMessage로 닫음
enum class ESystemMessageType : uint8
{
    Notice,
    Waiting,
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

    // 위젯을 보이게/숨기게 한다(이미 그 상태면 그대로). 창이면 입력 모드(커서)도 갱신한다.
    void SetWidgetVisible(FName WidgetName, bool bVisible);

    UUserWidget* GetWidget(FName WidgetName);

    // 마우스 커서 등 입력을 제어하는 내부 함수
    void UpdateInputMode();

    // 창이 없을 때의 입력 모드. true = 전투 입력(인게임), false = UI 전용 + 커서(로그인 화면). 각 GameMode가 정한다.
    void SetGameplayInput(bool bEnable);

    // 커서 모드(UI6, D-274): Ctrl로 켜고 끈다. 켜져 있거나 창이 하나라도 열려 있으면 커서가 보이고(게임 입력도 받음) 마우스로 카메라가 돌지 않는다.
    void SetCursorMode(bool bEnable);
    bool IsCursorMode() const { return bCursorMode; }
    bool HasOpenWindow() const { return OpenedWindows.Num() > 0; }
    // 인게임에서 지금 커서가 보이는가(커서 모드 또는 창 열림). 월드 좌클릭 공격·마우스 카메라 회전을 막는 기준
    bool IsCursorShown() const { return bGameplayInput && (bCursorMode || OpenedWindows.Num() > 0); }

    // 시스템 메시지 창. 한 번에 하나만 보이고 나머지는 차례를 기다린다(Waiting은 맨 앞에 끼운다).
    // 위젯 클래스는 UJMGameInstance::SystemMessageWidgetClass. 레벨 전환 뒤에도 남은 메시지는 새 레벨에서 띄운다.
    void ShowSystemMessage(const FText& Message, ESystemMessageType Type = ESystemMessageType::Notice);

    // 사라지는 알림 줄(D-266): 확인 버튼 없이 HUD 위쪽에 잠깐 띄운다. HUD가 없으면(레벨 전환 중) 버린다.
    void ShowNotice(const FText& Message);

    // 지금 보이는 시스템 메시지를 닫고 다음 메시지를 띄운다.
    void CloseSystemMessage();

    // 대기(Waiting) 메시지만 닫는다. 떠 있는 것이 확인 메시지면 그대로 둔다.
    void CloseWaitingMessage();

private:
    struct FPendingSystemMessage
    {
        FText Message;
        ESystemMessageType Type;
    };

    void ShowNextSystemMessage();

private:
    // 모든 위젯을 이름으로 보관하는 사전(Dictionary)
    UPROPERTY()
    TMap<FName, FWidgetInfo> ManagedWidgets;

    // 현재 화면에 켜져 있는 '창(Window)'들의 이름을 담아두는 스택
    TArray<FName> OpenedWindows;

    // 입력을 제어하기 위해 플레이어 컨트롤러를 기억해 둡니다.
    UPROPERTY()
    TObjectPtr<APlayerController> Controller;

    bool bGameplayInput = false;
    bool bCursorMode = false;

    // 아직 띄우지 못한 시스템 메시지와, 지금 화면에 떠 있는지 여부
    TArray<FPendingSystemMessage> PendingSystemMessages;
    bool bSystemMessageShown = false;

};
