#pragma once

#include "CoreMinimal.h"
#include "UI/JMUserWidget.h"
#include "PlayerMenuWidget.generated.h"

// 다른 플레이어 메뉴(UI6, D-274): 커서 모드에서 플레이어를 우클릭하면 그 캐릭터 머리 옆에 뜬다(이름 + 거래 신청·파티 초대·닫기).
// 버튼은 요청만 보내고(거리·상태 검증은 서버) 메뉴를 닫는다. 항목을 늘리면 WBP_PlayerMenu에 Btn* 버튼 줄을 더하고 여기에 처리 함수를 둔다.
// 대상이 시야에서 사라지거나 죽거나 멀어지면(거래 범위의 2배) 스스로 닫는다. 창으로 등록되어 떠 있는 동안 커서가 보인다.
UCLASS()
class JM_API UPlayerMenuWidget : public UJMUserWidget
{
	GENERATED_BODY()

public:
	void OpenFor(int32 PlayerId);
	void Close();
	int32 GetTargetId() const { return TargetId; }

	// 버튼 처리(개발 확인용으로 에디터 MCP·콘솔 PlayerMenu에서도 부른다)
	UFUNCTION(BlueprintCallable, Category = "PlayerMenu")
	void OnTradeClicked();
	UFUNCTION(BlueprintCallable, Category = "PlayerMenu")
	void OnPartyClicked();
	UFUNCTION(BlueprintCallable, Category = "PlayerMenu")
	void OnCloseClicked();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 메뉴 바탕(캔버스 패널 안의 Border — 이름 Panel이면 공용 창 바탕이 입혀진다). 위치를 매 틱 대상 머리 옆으로 옮긴다
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> Panel;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtTitleName;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnTrade;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnParty;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnClose;

	// 머리 위치(화면)에서 메뉴 왼쪽 위 모서리까지(위젯 좌표, DPI 적용 뒤)
	UPROPERTY(EditAnywhere, Category = "PlayerMenu")
	FVector2D ScreenOffset = FVector2D(40.f, -30.f);
	// 머리 위치 = 캡슐 위 끝에서 이만큼 위(cm)
	UPROPERTY(EditAnywhere, Category = "PlayerMenu")
	float HeadOffsetCm = 10.f;

private:
	const class AJMPlayer* FindTarget() const;
	// 대상 머리 옆으로 옮긴다. 대상이 없거나 화면 밖(카메라 뒤)이면 false
	bool UpdatePosition();

	int32 TargetId = 0;
};
