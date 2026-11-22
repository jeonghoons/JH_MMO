#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/JMUserWidget.h"
#include "NameplateWidget.generated.h"

// 마을 NPC 머리 위 이름표 (Q1, D-265): 위 줄 = 퀘스트 표식("!" 받을 퀘스트 있음, "?" 완료할 수 있음 — UJMQuestManager::GetNpcMarker), 아래 줄 = 이름.
// UJMObjectManager가 NPC를 만들 때 화면 공간 UWidgetComponent로 붙인다. WBP 없이 C++가 위젯 트리를 만든다
// (같은 이름의 TextBlock을 가진 WBP 자식 클래스를 만들면 그쪽 배치를 쓴다).
UCLASS()
class JM_API UNameplateWidget : public UJMUserWidget
{
	GENERATED_BODY()

public:
	// 이름과 NPC 종류 ID(NpcTable ID — 표식 계산용)
	void Setup(const FString& Name, int32 InNpcTemplateId);

protected:
	// 위젯 컴포넌트가 만든 위젯은 플레이어 정보(PlayerContext)가 없어 NativeOnInitialized가 불리지 않으므로 Initialize에서 트리를 만든다.
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtMarker;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtName;

private:
	void RefreshMarker();

	int32 NpcTemplateId = 0;
	FString ShownMarker;
	FDelegateHandle QuestChangedHandle;
};
