#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ReviveWidget.generated.h"

// 부활 창(A2.4, D-149). 내 캐릭터가 죽으면 UJMObjectManager::HandleDead가 띄우고, SC_REVIVE를 받으면 HandleRevive가 숨긴다.
// 대기 시간(SC_DEAD.revive_delay_ms, 서버 값)이 지나면 버튼이 켜지고, 누르면 CS_REVIVE를 보낸다(검증은 서버).
UCLASS()
class JM_API UReviveWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 대기 시간을 다시 재기 시작한다. DelayMs가 0 이하면 바로 누를 수 있다.
	void StartCountdown(int32 DelayMs);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void OnReviveClicked();

	void Refresh();

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UTextBlock> TxtMessage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> BtnRevive;

	// 요청을 보낸 뒤 응답(SC_REVIVE)이 없을 때 버튼을 다시 켜기까지의 시간
	UPROPERTY(EditDefaultsOnly, Category = "Revive")
	float RetrySec = 1.5f;

private:
	double ReadyTime = 0.0;      // 이 시각(FPlatformTime 초)부터 부활 요청 가능
	double RetryTime = 0.0;      // 요청을 보낸 뒤 이 시각까지 버튼을 잠근다
	int32 ShownSeconds = -1;     // 문구에 표시한 남은 초(바뀔 때만 다시 쓴다)
};
