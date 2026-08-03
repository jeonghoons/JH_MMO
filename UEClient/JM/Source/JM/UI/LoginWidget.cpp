#include "UI/LoginWidget.h"
#include "Components/EditableText.h"
#include "Components/Button.h"
#include "Network/NetworkManager.h"
#include "Protocol/Protocol.pb.h"
#include "Network/SendBuffer.h"

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

	UNetworkManager* NetManager = GetGameInstance()->GetSubsystem<UNetworkManager>();
	if (NetManager)
	{
		
		Protocol::CS_LOGIN_PACKET pkt;
		pkt.set_account_id(TCHAR_TO_UTF8(*AccountId));
		pkt.set_account_pw(TCHAR_TO_UTF8(*AccountPw));
		pkt.set_is_dummy(false);

		TSharedPtr<SendBuffer> sendBuffer = SendBuffer::MakeSendBuffer(pkt, Protocol::CS_LOGIN);
		NetManager->SendPacket(sendBuffer);
	}
}

void ULoginWidget::OnSignUpButtonClicked()
{
	FString AccountId = EditAccountId ? EditAccountId->GetText().ToString() : TEXT("");
	FString AccountPw = EditAccountPassword ? EditAccountPassword->GetText().ToString() : TEXT("");

	UNetworkManager* NetManager = GetGameInstance()->GetSubsystem<UNetworkManager>();
	if (NetManager)
	{
		Protocol::CS_SIGNUP_PACKET pkt;
		pkt.set_account_id(TCHAR_TO_UTF8(*AccountId));
		pkt.set_account_pw(TCHAR_TO_UTF8(*AccountPw));

		TSharedPtr<SendBuffer> sendBuffer = SendBuffer::MakeSendBuffer(pkt, Protocol::CS_SIGNUP);
		NetManager->SendPacket(sendBuffer);
	}
}
