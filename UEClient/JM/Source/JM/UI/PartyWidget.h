#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/JMUserWidget.h"
#include "PartyWidget.generated.h"

// 파티 창 (DG2, DESIGN_PARTY_DUNGEON §5-5). 한 위젯에 화면 두 개가 있다.
//  - PanelInvite: 받은 초대의 수락·거절
//  - PanelParty: 파티원 목록(파티장 표시·이름·레벨·있는 곳), 탈퇴 버튼. 파티가 없으면 안내 글만
// 내용은 UJMPartyManager(서버가 보낸 상태의 사본)를 그대로 그린다. 조작은 요청만 보내고 화면은 서버 패킷이 와야 바뀐다.
// P 키(AJMMyPlayer::OnPartyInput)로 여닫고, 초대를 받거나 파티가 처음 맺어지면 UJMPartyManager가 띄운다.
UCLASS()
class JM_API UPartyWidget : public UJMUserWidget
{
	GENERATED_BODY()

public:
	// 개발 확인용(에디터 MCP — 마우스 없이 버튼 처리 함수를 부른다)
	UFUNCTION(BlueprintCallable, Category = "Party")
	void OnAcceptClicked();
	UFUNCTION(BlueprintCallable, Category = "Party")
	void OnDeclineClicked();
	UFUNCTION(BlueprintCallable, Category = "Party")
	void OnLeaveClicked();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ---- 초대 화면 ----
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> PanelInvite;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtInvite;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnAccept;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnDecline;

	// ---- 파티 화면 ----
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> PanelParty;
	// 파티원 줄을 넣는 곳(VerticalBox 등 아무 패널). 줄은 C++가 TextBlock으로 만든다
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> MemberList;
	// 파티가 없을 때의 안내 글
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtEmpty;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnLeave;

	// 파티원 줄의 글자 크기
	UPROPERTY(EditAnywhere, Category = "Party")
	int32 MemberFontSize = 16;

private:
	class UJMPartyManager* GetPartyManager() const;
	void Refresh();

	UPROPERTY()
	TArray<TObjectPtr<class UTextBlock>> MemberRows;
	FDelegateHandle PartyChangedHandle;
};
