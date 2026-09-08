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
	void AddChatMessage(int32 SenderId, const FString& Message);
protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	UEditableText* ChatInput;

	UPROPERTY(meta = (BindWidget))
	UScrollBox* ChatHistoryBox;

private:
	UFUNCTION()
	void OnChatTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);
};
