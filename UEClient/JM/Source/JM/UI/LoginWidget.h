#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/JMUserWidget.h"
#include "LoginWidget.generated.h"

// 로그인 화면(UI7, D-275): 로그인 창 ↔ 가입 창 전환, Enter = 확정, 서버 연결 상태 표시, 배경 천천히 확대.
// 새 위젯은 모두 BindWidgetOptional이라 옛 WBP(한 창에 입력칸·버튼)도 그대로 동작한다.
UCLASS()
class JM_API ULoginWidget : public UJMUserWidget
{
	GENERATED_BODY()

public:
	// 서버의 가입 결과(SC_SIGNUP). 성공이면 로그인 창으로 돌아가 가입한 아이디를 채운다.
	void OnSignUpResult(bool bSuccess);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void OnLoginButtonClicked();

	UFUNCTION()
	void OnSignUpButtonClicked();

	UFUNCTION()
	void OnGoSignUpClicked();

	UFUNCTION()
	void OnBackClicked();

	UFUNCTION()
	void OnBodyMaleClicked();

	UFUNCTION()
	void OnBodyFemaleClicked();

	UFUNCTION()
	void OnReconnectClicked();

	UFUNCTION()
	void OnLoginTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void OnSignUpTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

protected:
	// ---- 로그인 창 ----
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UEditableText> EditAccountId;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UEditableText> EditAccountPassword;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> BtnLogin;

	// 가입 창으로 넘어가는 버튼("계정 만들기"). 없으면 옛 배치: BtnSignUp이 로그인 칸의 값으로 바로 가입한다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnGoSignUp;

	// ---- 가입 창 ----
	// 0 = 로그인 창, 1 = 가입 창
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidgetSwitcher> SwitchMode;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UEditableText> EditSignUpId;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UEditableText> EditSignUpPassword;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UEditableText> EditSignUpPasswordConfirm;

	// 가입 확정 버튼(옛 배치에서는 로그인 창에 있음 — 이름은 WBP 바인딩 때문에 유지)
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> BtnSignUp;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnBack;

	// 체형 버튼(가입 후 바꿀 수 없음). 고른 쪽은 그대로, 아닌 쪽은 BodyUnselectedOpacity로 흐리게.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnBodyMale;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnBodyFemale;

	// 옛 배치의 체형 콤보. 체형 버튼이 없을 때만 쓴다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UComboBoxString> ComboHero;

	// ---- 서버 연결 상태 ----
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UImage> ImgServerDot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtServerStatus;

	// 끊겼을 때만 보인다
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnReconnect;

	// ---- 배경 ----
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UImage> ImgBackground;

	// 배경 확대 폭(1 + 이 값까지 커졌다 돌아옴)과 한 번 왕복하는 시간(초). 0이면 움직이지 않음.
	UPROPERTY(EditAnywhere, Category = "Login")
	float BackgroundZoomAmount = 0.05f;

	UPROPERTY(EditAnywhere, Category = "Login")
	float BackgroundZoomPeriodSec = 40.0f;

	// 고르지 않은 체형 버튼의 불투명도(버튼 바탕이 어두워 색을 곱하면 차이가 안 보임)
	UPROPERTY(EditAnywhere, Category = "Login")
	float BodyUnselectedOpacity = 0.4f;

	// 서버 상태를 다시 보는 간격(초)
	UPROPERTY(EditAnywhere, Category = "Login")
	float ServerStatusIntervalSec = 0.5f;

private:
	// 고른 체형의 Protocol::PlayerType 값.
	int32 GetSelectedHeroType() const;

	// 서버에 연결돼 있지 않으면 다시 연결을 시도한다. 연결되면 true(실패 메시지는 ConnectToServer가 띄운다).
	bool EnsureConnected(class UNetworkManager* NetManager) const;

	void SetMode(int32 Mode);
	void SelectBody(int32 Index);
	void RefreshServerStatus();
	void FocusFirstField();

	// 시험용 자동 로그인(배포 빌드 제외): Saved/TestLogin.txt가 있을 때만 그 계정으로 로그인한다.
	// 줄마다 "계정 비밀번호"(예: Test1 1234), PIE 창 번호 순서대로 한 줄씩 쓴다(창이 하나면 첫 줄). 게임 실행당 한 번만 시도한다.
	void TryTestAutoLogin();
	FTimerHandle TestAutoLoginTimer;
	FTimerHandle ServerStatusTimer;

	int32 SelectedBody = 0;
	int32 LastServerState = -1;		// -1 = 아직 그리지 않음, 0 = 끊김, 1 = 연결됨
	float BackgroundTime = 0.0f;
	FString PendingSignUpId;		// 가입 요청을 보낸 아이디(성공하면 로그인 칸에 채움)
};
