#include "UI/LoginWidget.h"
#include "Components/EditableText.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Network/NetworkManager.h"
#include "Protocol/Enum.pb.h"

namespace
{
	// 가입 시 고를 수 있는 체형(가입 후 바꿀 수 없음). 콤보박스 표시 순서와 같다. 서버는 MALE/FEMALE 외에는 거부한다.
	struct FHeroOption
	{
		const TCHAR* Label;
		Protocol::PlayerType Type;
	};
	const FHeroOption HeroOptions[] = {
		{ TEXT("남성"), Protocol::PLAYER_TYPE_MALE },
		{ TEXT("여성"), Protocol::PLAYER_TYPE_FEMALE },
	};
}

void ULoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ComboHero)
	{
		ComboHero->ClearOptions();
		for (const FHeroOption& Option : HeroOptions)
		{
			ComboHero->AddOption(Option.Label);
		}
		ComboHero->SetSelectedIndex(0);
	}

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

	UNetworkManager* NetManager = UNetworkManager::Get(this);
	if (EnsureConnected(NetManager))
	{
		NetManager->SendLogin(AccountId, AccountPw);
	}
}

void ULoginWidget::OnSignUpButtonClicked()
{
	FString AccountId = EditAccountId ? EditAccountId->GetText().ToString() : TEXT("");
	FString AccountPw = EditAccountPassword ? EditAccountPassword->GetText().ToString() : TEXT("");

	UNetworkManager* NetManager = UNetworkManager::Get(this);
	if (EnsureConnected(NetManager))
	{
		NetManager->SendSignUp(AccountId, AccountPw, GetSelectedHeroType());
	}
}

bool ULoginWidget::EnsureConnected(UNetworkManager* NetManager) const
{
	if (NetManager == nullptr) return false;

	if (!NetManager->IsConnected())
	{
		NetManager->ConnectToServer();
	}
	return NetManager->IsConnected();
}

int32 ULoginWidget::GetSelectedHeroType() const
{
	const int32 Index = ComboHero ? ComboHero->GetSelectedIndex() : 0;
	return HeroOptions[FMath::Clamp(Index, 0, static_cast<int32>(UE_ARRAY_COUNT(HeroOptions)) - 1)].Type;
}
