#include "UI/LoginWidget.h"
#include "Components/EditableText.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/ComboBoxString.h"
#include "Network/NetworkManager.h"
#include "Protocol/Enum.pb.h"

namespace
{
	// 가입 시 고를 수 있는 영웅. 콤보박스 표시 순서와 같다.
	struct FHeroOption
	{
		const TCHAR* Label;
		Protocol::PlayerType Type;
	};
	const FHeroOption HeroOptions[] = {
		{ TEXT("전사 (Greystone)"), Protocol::PLAYER_TYPE_WARRIOR },
		{ TEXT("궁수 (Sparrow)"), Protocol::PLAYER_TYPE_ARCHER },
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

	ShowResult(FText::GetEmpty());

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
		NetManager->SendSignUp(AccountId, AccountPw, GetSelectedHeroType());
	}
}

void ULoginWidget::ShowResult(const FText& Message)
{
	if (TxtResult)
	{
		TxtResult->SetText(Message);
	}
	if (!Message.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("Login screen: %s"), *Message.ToString());
	}
}

int32 ULoginWidget::GetSelectedHeroType() const
{
	const int32 Index = ComboHero ? ComboHero->GetSelectedIndex() : 0;
	return HeroOptions[FMath::Clamp(Index, 0, static_cast<int32>(UE_ARRAY_COUNT(HeroOptions)) - 1)].Type;
}
