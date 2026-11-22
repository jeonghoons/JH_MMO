#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/JMUserWidget.h"
#include "ChatWidget.generated.h"

class UEditableText;
class UScrollBox;

UCLASS()
class JM_API UChatWidget : public UJMUserWidget
{
	GENERATED_BODY()
public:
	void FocusChatInput(class APlayerController* PC);
	// SC_CHAT 수신 시 호출. "보낸 사람: 내용" 한 줄을 기록에 추가한다.
	void AddChatMessage(const FString& SenderName, const FString& Message);
protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	UEditableText* ChatInput;

	UPROPERTY(meta = (BindWidget))
	UScrollBox* ChatHistoryBox;

	// 채팅 기록에 남길 최대 줄 수
	UPROPERTY(EditDefaultsOnly, Category = "Chat")
	int32 MaxHistoryLines = 100;

private:
	UFUNCTION()
	void OnChatTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	// "/"로 시작하는 입력을 명령으로 처리한다(DG2: /파티초대 이름, /파티탈퇴). 명령이었으면 true(채팅으로 보내지 않음)
	bool HandleCommand(const FString& Input);
};
