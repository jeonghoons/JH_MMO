#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SystemMessageWidget.generated.h"

// 시스템 메시지 창(로그인 실패, 방 입장 대기 등). 띄우고 닫는 일은 UJMUIManager::ShowSystemMessage/CloseSystemMessage가 한다.
UCLASS()
class JM_API USystemMessageWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 확인 메시지(bWaiting = false)는 확인 버튼과 반투명 배경, 대기 메시지는 버튼 없이 불투명 배경(코드가 닫는다).
	void Setup(const FText& Message, bool bWaiting);

protected:
	virtual void NativeConstruct() override;

	UFUNCTION()
	void OnOkClicked();

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UTextBlock> TxtMessage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> BtnOk;

	// 화면 전체를 덮는 배경. 메시지 종류에 따라 색을 바꾼다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UBorder> Dim;

	// 확인 메시지 배경: 뒤 화면(로그인 창 등)이 비쳐 보인다.
	UPROPERTY(EditDefaultsOnly, Category = "SystemMessage")
	FLinearColor NoticeDimColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);

	// 대기 메시지 배경: 방 입장 중 맵이 그려지는 모습을 가린다.
	UPROPERTY(EditDefaultsOnly, Category = "SystemMessage")
	FLinearColor WaitingDimColor = FLinearColor(0.f, 0.f, 0.f, 1.f);
};
