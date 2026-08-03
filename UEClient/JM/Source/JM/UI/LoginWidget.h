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
};
