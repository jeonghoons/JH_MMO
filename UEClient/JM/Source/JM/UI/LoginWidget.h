#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LoginWidget.generated.h"

UCLASS()
class JM_API ULoginWidget : public UUserWidget
{
	GENERATED_BODY()

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

	// 가입할 영웅 선택. 옵션은 NativeConstruct에서 채운다. WBP에 없으면 전사로 가입한다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UComboBoxString> ComboHero;

private:
	// ComboHero에서 고른 영웅의 Protocol::PlayerType 값.
	int32 GetSelectedHeroType() const;

	// 서버에 연결돼 있지 않으면 다시 연결을 시도한다. 연결되면 true(실패 메시지는 ConnectToServer가 띄운다).
	bool EnsureConnected(class UNetworkManager* NetManager) const;
};
