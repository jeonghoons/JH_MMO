#include "UI/LoginWidget.h"
#include "Components/EditableText.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/WidgetSwitcher.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Network/NetworkManager.h"
#include "Protocol/Enum.pb.h"
#include "UI/JMUIManager.h"
#include "UI/UIStyle.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

namespace
{
	// 가입 시 고를 수 있는 체형(가입 후 바꿀 수 없음). 버튼·콤보박스 순서와 같다. 서버는 MALE/FEMALE 외에는 거부한다.
	struct FHeroOption
	{
		const TCHAR* Label;
		Protocol::PlayerType Type;
	};
	const FHeroOption HeroOptions[] = {
		{ TEXT("남성"), Protocol::PLAYER_TYPE_MALE },
		{ TEXT("여성"), Protocol::PLAYER_TYPE_FEMALE },
	};

	FString GetText(const UEditableText* Edit)
	{
		return Edit ? Edit->GetText().ToString().TrimStartAndEnd() : FString();
	}
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

	if (BtnLogin)		BtnLogin->OnClicked.AddDynamic(this, &ULoginWidget::OnLoginButtonClicked);
	if (BtnSignUp)		BtnSignUp->OnClicked.AddDynamic(this, &ULoginWidget::OnSignUpButtonClicked);
	if (BtnGoSignUp)	BtnGoSignUp->OnClicked.AddDynamic(this, &ULoginWidget::OnGoSignUpClicked);
	if (BtnBack)		BtnBack->OnClicked.AddDynamic(this, &ULoginWidget::OnBackClicked);
	if (BtnBodyMale)	BtnBodyMale->OnClicked.AddDynamic(this, &ULoginWidget::OnBodyMaleClicked);
	if (BtnBodyFemale)	BtnBodyFemale->OnClicked.AddDynamic(this, &ULoginWidget::OnBodyFemaleClicked);
	if (BtnReconnect)	BtnReconnect->OnClicked.AddDynamic(this, &ULoginWidget::OnReconnectClicked);

	// Enter = 그 창의 확정 버튼
	for (UEditableText* Edit : { EditAccountId.Get(), EditAccountPassword.Get() })
	{
		if (Edit) Edit->OnTextCommitted.AddDynamic(this, &ULoginWidget::OnLoginTextCommitted);
	}
	for (UEditableText* Edit : { EditSignUpId.Get(), EditSignUpPassword.Get(), EditSignUpPasswordConfirm.Get() })
	{
		if (Edit) Edit->OnTextCommitted.AddDynamic(this, &ULoginWidget::OnSignUpTextCommitted);
	}

	SetMode(0);
	SelectBody(0);
	LastServerState = -1;
	RefreshServerStatus();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ServerStatusTimer, this, &ULoginWidget::RefreshServerStatus, FMath::Max(0.1f, ServerStatusIntervalSec), true);
		// 화면이 그려진 뒤에 포커스를 줘야 입력칸이 받는다.
		World->GetTimerManager().SetTimerForNextTick(this, &ULoginWidget::FocusFirstField);
#if !UE_BUILD_SHIPPING
		// 화면이 뜨고 서버 연결이 끝날 시간을 준 뒤 한 번 시도한다.
		World->GetTimerManager().SetTimer(TestAutoLoginTimer, this, &ULoginWidget::TryTestAutoLogin, 1.0f, false);
#endif
	}
}

void ULoginWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ServerStatusTimer);
		World->GetTimerManager().ClearTimer(TestAutoLoginTimer);
	}
	Super::NativeDestruct();
}

void ULoginWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 배경을 1 → 1 + Amount → 1로 천천히 확대·축소(코사인이라 끝에서 부드럽게 멈췄다 돌아감)
	if (ImgBackground && BackgroundZoomAmount > 0.0f && BackgroundZoomPeriodSec > 0.0f)
	{
		BackgroundTime = FMath::Fmod(BackgroundTime + InDeltaTime, BackgroundZoomPeriodSec);
		const float Phase = 0.5f - 0.5f * FMath::Cos(2.0f * PI * BackgroundTime / BackgroundZoomPeriodSec);
		const float Scale = 1.0f + BackgroundZoomAmount * Phase;
		ImgBackground->SetRenderScale(FVector2D(Scale, Scale));
	}
}

void ULoginWidget::TryTestAutoLogin()
{
#if !UE_BUILD_SHIPPING
	UNetworkManager* NetManager = UNetworkManager::Get(this);
	if (NetManager == nullptr || NetManager->bTestAutoLoginTried) return;

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectSavedDir() / TEXT("TestLogin.txt")))) return;   // 파일이 없으면 평소대로 손으로 로그인

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	const int32 PieIndex = GetWorld() ? FMath::Max(0, GetWorld()->GetOutermost()->GetPIEInstanceID()) : 0;
	if (!Lines.IsValidIndex(PieIndex)) return;

	FString AccountId, AccountPw;
	if (!Lines[PieIndex].TrimStartAndEnd().Split(TEXT(" "), &AccountId, &AccountPw)) return;
	AccountPw.TrimStartAndEndInline();

	NetManager->bTestAutoLoginTried = true;
	UE_LOG(LogTemp, Log, TEXT("[Login] test auto login: %s (Saved/TestLogin.txt line %d)"), *AccountId, PieIndex + 1);
	if (EnsureConnected(NetManager))
	{
		NetManager->SendLogin(AccountId, AccountPw);
	}
#endif
}

void ULoginWidget::OnLoginButtonClicked()
{
	const FString AccountId = GetText(EditAccountId);
	const FString AccountPw = EditAccountPassword ? EditAccountPassword->GetText().ToString() : FString();

	UNetworkManager* NetManager = UNetworkManager::Get(this);
	if (EnsureConnected(NetManager))
	{
		NetManager->SendLogin(AccountId, AccountPw);
	}
}

void ULoginWidget::OnSignUpButtonClicked()
{
	// 가입 창이 있으면 가입 창의 칸, 없으면(옛 배치) 로그인 칸의 값으로 가입한다.
	const bool bSignUpPanel = EditSignUpId != nullptr;
	const FString AccountId = bSignUpPanel ? GetText(EditSignUpId) : GetText(EditAccountId);
	UEditableText* PwEdit = bSignUpPanel ? EditSignUpPassword.Get() : EditAccountPassword.Get();
	const FString AccountPw = PwEdit ? PwEdit->GetText().ToString() : FString();

	UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr;
	if (AccountId.IsEmpty() || AccountPw.IsEmpty())
	{
		if (UIManager) UIManager->ShowSystemMessage(FText::FromString(TEXT("아이디와 비밀번호를 입력해 주세요.")));
		return;
	}
	if (EditSignUpPasswordConfirm && EditSignUpPasswordConfirm->GetText().ToString() != AccountPw)
	{
		if (UIManager) UIManager->ShowSystemMessage(FText::FromString(TEXT("비밀번호 확인이 일치하지 않습니다.")));
		return;
	}

	UNetworkManager* NetManager = UNetworkManager::Get(this);
	if (EnsureConnected(NetManager))
	{
		PendingSignUpId = AccountId;
		NetManager->SendSignUp(AccountId, AccountPw, GetSelectedHeroType());
	}
}

void ULoginWidget::OnSignUpResult(bool bSuccess)
{
	if (!bSuccess || PendingSignUpId.IsEmpty()) return;

	if (EditAccountId) EditAccountId->SetText(FText::FromString(PendingSignUpId));
	if (EditAccountPassword) EditAccountPassword->SetText(FText::GetEmpty());
	for (UEditableText* Edit : { EditSignUpId.Get(), EditSignUpPassword.Get(), EditSignUpPasswordConfirm.Get() })
	{
		if (Edit) Edit->SetText(FText::GetEmpty());
	}
	PendingSignUpId.Reset();
	SetMode(0);
	if (EditAccountPassword) EditAccountPassword->SetKeyboardFocus();
}

void ULoginWidget::OnGoSignUpClicked()
{
	SetMode(1);
	// 로그인 칸에 적어 둔 아이디가 있으면 가입 칸에 옮겨 준다.
	if (EditSignUpId && GetText(EditSignUpId).IsEmpty())
	{
		EditSignUpId->SetText(FText::FromString(GetText(EditAccountId)));
	}
	if (EditSignUpId) EditSignUpId->SetKeyboardFocus();
}

void ULoginWidget::OnBackClicked()
{
	SetMode(0);
	FocusFirstField();
}

void ULoginWidget::OnBodyMaleClicked()		{ SelectBody(0); }
void ULoginWidget::OnBodyFemaleClicked()	{ SelectBody(1); }

void ULoginWidget::OnReconnectClicked()
{
	EnsureConnected(UNetworkManager::Get(this));
	RefreshServerStatus();
}

void ULoginWidget::OnLoginTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter) OnLoginButtonClicked();
}

void ULoginWidget::OnSignUpTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter) OnSignUpButtonClicked();
}

void ULoginWidget::SetMode(int32 Mode)
{
	if (SwitchMode) SwitchMode->SetActiveWidgetIndex(Mode);
}

void ULoginWidget::SelectBody(int32 Index)
{
	SelectedBody = FMath::Clamp(Index, 0, static_cast<int32>(UE_ARRAY_COUNT(HeroOptions)) - 1);
	if (BtnBodyMale)	BtnBodyMale->SetRenderOpacity(SelectedBody == 0 ? 1.0f : BodyUnselectedOpacity);
	if (BtnBodyFemale)	BtnBodyFemale->SetRenderOpacity(SelectedBody == 1 ? 1.0f : BodyUnselectedOpacity);
}

void ULoginWidget::RefreshServerStatus()
{
	const UNetworkManager* NetManager = UNetworkManager::Get(this);
	const int32 State = (NetManager && NetManager->IsConnected()) ? 1 : 0;
	if (State == LastServerState) return;
	LastServerState = State;

	if (ImgServerDot)
	{
		ImgServerDot->SetColorAndOpacity(State
			? JMUIStyle::Color(this, TEXT("ServerOnline"), FLinearColor(0.1f, 0.8f, 0.2f))
			: JMUIStyle::Color(this, TEXT("ServerOffline"), FLinearColor(0.85f, 0.15f, 0.1f)));
	}
	if (TxtServerStatus)
	{
		TxtServerStatus->SetText(FText::FromString(State ? TEXT("서버 연결됨") : TEXT("서버 연결 안 됨")));
	}
	if (BtnReconnect)
	{
		BtnReconnect->SetVisibility(State ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
}

void ULoginWidget::FocusFirstField()
{
	if (EditAccountId) EditAccountId->SetKeyboardFocus();
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
	// 체형 버튼이 있으면 버튼, 없으면 옛 콤보박스
	const int32 Index = (BtnBodyMale || BtnBodyFemale) ? SelectedBody : (ComboHero ? ComboHero->GetSelectedIndex() : 0);
	return HeroOptions[FMath::Clamp(Index, 0, static_cast<int32>(UE_ARRAY_COUNT(HeroOptions)) - 1)].Type;
}
