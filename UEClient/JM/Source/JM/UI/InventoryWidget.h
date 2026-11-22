#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/JMUserWidget.h"
#include "InventoryWidget.generated.h"

class UItemSlotWidget;

// 인벤토리 창 + 장비창 (A6.1·MC4.4, D-166): 장착 7칸, 인벤토리 칸 격자, 골드, 선택한 아이템 이름.
// 내용은 UJMInventoryManager(서버가 보낸 값의 사본)를 그대로 그린다 — 창을 여닫을 때 서버에 묻지 않는다.
// 조작은 요청만 보낸다: 인벤토리 장비 우클릭 = 장착, 장착 칸 우클릭 = 해제, 끌어다 놓기 = 칸 옮기기·맞바꾸기·장착·해제.
// 화면은 서버의 SC_INVENTORY_UPDATE가 와야 바뀌고, 거부는 시스템 메시지로 나온다.
UCLASS()
class JM_API UInventoryWidget : public UJMUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 칸 위젯(WBP_ItemSlot). 인벤토리 칸과 장착 칸을 이 클래스로 만든다.
	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UItemSlotWidget> ItemSlotClass;

	// 인벤토리 격자의 열 수. 칸 수는 서버가 알려 준다(SC_INVENTORY.max_slots).
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (ClampMin = "1"))
	int32 Columns = 6;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UUniformGridPanel> BagGrid;

	// 장착 칸 7개를 넣는 곳(세로 상자 등 아무 패널).
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> EquipPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtGold;

	// "쓰는 칸 / 전체 칸"
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtSlots;

	// 선택한 아이템의 이름(스택이면 수량)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtSelected;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnClose;

	// 선택한 인벤토리 아이템을 바닥에 버린다(스택 통째, A6.2·D-155). 장착 중인 것은 버릴 수 없어 꺼 둔다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnDrop;

	// 내 캐릭터 앞모습(UI5, D-272). 창이 보이는 동안만 매 프레임 찍는다 — 내 캐릭터와 같은 외형의 플레이어 액터를
	// 플레이 영역 밖(PreviewLocation)에 하나 더 두고 그 액터만 보이게 찍음(이동·충돌·Tick 끔, 무기는 등에 멘 Idle)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UImage> ImgCharacter;

	UPROPERTY(EditAnywhere, Category = "Preview")
	FIntPoint PreviewSize = FIntPoint(400, 600);
	UPROPERTY(EditAnywhere, Category = "Preview")
	FVector PreviewLocation = FVector(0.f, 0.f, 60000.f);
	// 카메라: 캐릭터 앞 거리·높이(캡슐 중심 기준)·가로 시야각
	UPROPERTY(EditAnywhere, Category = "Preview")
	float PreviewDistance = 280.f;
	UPROPERTY(EditAnywhere, Category = "Preview")
	float PreviewHeight = 5.f;
	UPROPERTY(EditAnywhere, Category = "Preview")
	float PreviewFov = 25.f;
	// 앞쪽 주광·뒤쪽 보조광(루멘). 키트 셰이더의 그늘이 어두워 월드 조명만으로는 앞모습이 검게 나옴
	UPROPERTY(EditAnywhere, Category = "Preview")
	float PreviewKeyLight = 900.f;
	UPROPERTY(EditAnywhere, Category = "Preview")
	float PreviewFillLight = 300.f;

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void UpdatePreview();
	bool EnsurePreview();
	void DestroyPreview();

	UPROPERTY(Transient)
	TObjectPtr<class AJMPlayer> PreviewActor;
	UPROPERTY(Transient)
	TObjectPtr<class USceneCaptureComponent2D> PreviewCapture;
	UPROPERTY(Transient)
	TObjectPtr<class UTextureRenderTarget2D> PreviewTarget;
	// 마지막으로 넣은 외형(내 캐릭터의 서버 값과 다르면 다시 넣음)
	int32 PreviewPlayerType = -1;
	TArray<int32> PreviewItems;

private:
	void Refresh();
	void BuildEquipSlots();
	void EnsureBagSlots(int32 SlotCount);
	UItemSlotWidget* CreateSlot();

	void OnSlotClicked(UItemSlotWidget* ItemSlot);
	void OnSlotRightClicked(UItemSlotWidget* ItemSlot);
	void OnSlotDropped(UItemSlotWidget* From, UItemSlotWidget* To);

	UFUNCTION()
	void OnCloseClicked();
	UFUNCTION()
	void OnDropClicked();

	UPROPERTY()
	TArray<TObjectPtr<UItemSlotWidget>> BagSlots;     // 인덱스 = 칸 번호
	UPROPERTY()
	TArray<TObjectPtr<UItemSlotWidget>> EquipSlots;   // 인덱스 = EquipPart - 1

	int64 SelectedDbId = 0;
	FDelegateHandle InventoryChangedHandle;
};
