#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "QuestWidget.generated.h"

class UAuctionRowButton;

// 퀘스트 창 (Q1, D-265). 내용은 UJMQuestManager(서버가 보낸 내 퀘스트의 사본 + 창 상태)를 그대로 그린다.
//   - 왼쪽: 퀘스트 줄(눌러서 고름). 대화 모드 = 그 NPC의 퀘스트 전부, 목록 모드(L 키) = 받은 퀘스트
//   - 오른쪽: 고른 퀘스트의 이름·권장 레벨·설명·목표와 진행·보상·상태
//   - 아래: [수락]/[완료](대화 모드에서만, 상태로 글자가 바뀜) · [닫기]
// 버튼은 요청만 보내고 화면은 SC_QUEST_UPDATE가 와야 바뀐다.
// 배치는 WBP_Quest(부모 이 클래스, D-266)가 정한다. 퀘스트 줄 버튼만 C++가 만든다(UAuctionRowButton — 경매 창 줄과 같은 버튼).
UCLASS()
class JM_API UQuestWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 개발 확인용(에디터 MCP — 마우스 없이 버튼 처리 함수를 부른다)
	UFUNCTION(BlueprintCallable, Category = "Quest")
	void OnActionClicked();
	UFUNCTION(BlueprintCallable, Category = "Quest")
	void OnCloseClicked();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtTitle;
	// 퀘스트 줄을 넣는 곳(VerticalBox 등 아무 패널). 줄은 C++가 만든다
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> QuestList;
	// 목록이 비었을 때의 안내
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtEmpty;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtDetail;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnAction;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtAction;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnClose;

	UPROPERTY(EditAnywhere, Category = "Quest")
	int32 RowFontSize = 15;

private:
	class UJMQuestManager* GetQuestManager() const;
	void Refresh();
	void OnRowClicked(int32 Index);
	FString BuildDetailText(int32 QuestId) const;

	UPROPERTY()
	TArray<TObjectPtr<UAuctionRowButton>> Rows;
	// 줄 번호 → 퀘스트 ID(마지막 Refresh 기준)
	TArray<int32> RowQuestIds;
	FDelegateHandle QuestChangedHandle;
};
