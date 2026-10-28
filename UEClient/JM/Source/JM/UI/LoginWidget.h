#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LoginWidget.generated.h"

UCLASS()
class JM_API ULoginWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 서버 응답(로그인 실패, 가입 결과)을 화면에 표시한다. TxtResult가 없으면 로그만 남긴다.
	void ShowResult(const FText& Message);

protected:
	virtual void NativeConstruct() override;

	UFUNCTION()
	void OnLoginButtonClicked();

	UFUNCTION()
	void OnSignUpButtonClicked();
	
protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UEditableText> EditAccountId;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UEditableText> EditAccountPassword;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> BtnLogin;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> BtnSignUp;

	// 서버 응답 표시용 텍스트. WBP에 같은 이름의 TextBlock이 없어도 동작한다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtResult;

	// 가입할 영웅 선택. 옵션은 NativeConstruct에서 채운다. WBP에 없으면 전사로 가입한다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UComboBoxString> ComboHero;

private:
	// ComboHero에서 고른 영웅의 Protocol::PlayerType 값.
	int32 GetSelectedHeroType() const;
};
