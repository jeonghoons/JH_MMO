#include "UI/LoginWidget.h"
#include "Components/EditableText.h"
#include "Components/Button.h"
#include "Network/NetworkManager.h"

void ULoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BtnLogin)
	{
		BtnLogin->OnClicked.AddDynamic(this, &ULoginWidget::OnLoginButtonClicked);
	}

	if (BtnSignUp)
	{
		BtnSignUp->OnClicked.AddDynamic(this, &ULoginWidget::OnSignUpButtonClicked);
	}
}

void ULoginWidget::OnLoginButtonClicked()
{
	FString AccountId = EditAccountId ? EditAccountId->GetText().ToString() : TEXT("");
	FString AccountPw = EditAccountPassword ? EditAccountPassword->GetText().ToString() : TEXT("");

	if (UNetworkManager* NetManager = UNetworkManager::Get(this))
	{
		NetManager->SendLogin(AccountId, AccountPw);
	}
}

void ULoginWidget::OnSignUpButtonClicked()
{
	FString AccountId = EditAccountId ? EditAccountId->GetText().ToString() : TEXT("");
	FString AccountPw = EditAccountPassword ? EditAccountPassword->GetText().ToString() : TEXT("");

	if (UNetworkManager* NetManager = UNetworkManager::Get(this))
	{
		NetManager->SendSignUp(AccountId, AccountPw);
	}
}
