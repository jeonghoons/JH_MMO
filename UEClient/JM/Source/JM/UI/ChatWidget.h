#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChatWidget.generated.h"

class UEditableText;
class UScrollBox;

UCLASS()
class JM_API UChatWidget : public UUserWidget
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
};
