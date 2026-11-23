#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/ItemTableRow.h"
#include "Player/QuickSlot.h"
#include "UI/JMUserWidget.h"
#include "CombatHudWidget.generated.h"

class USkillSlotWidget;

// 전투 HUD(D-038·D-148): 내 이름·레벨, HP·MP 바·수치, EXP 바·수치, 액티브 칸 5개. 값은 모두 서버 패킷에서 온다(UJMObjectManager가 갱신).
UCLASS()
class JM_API UCombatHudWidget : public UJMUserWidget
{
	GENERATED_BODY()

public:
	void SetHp(int32 Hp, int32 MaxHp);

	// 내 이름·레벨(ObjectInfo.name·level). 레벨업(SC_LEVEL_UP) 때 다시 부른다.
	void SetIdentity(const FString& Name, int32 Level);

	void SetMp(int32 Mp, int32 MaxMp);

	// EXP 바: Exp = 지금 레벨 안의 누적 경험치(서버 값), 분모는 PlayerStatTable.ExpToNext(Level). 분모가 0(최대 레벨)이면 가득 + "MAX" (A2.3b)
	void SetExp(int64 Exp, int32 Level);

	// 액티브 칸 내용(AJMMyPlayer::GetQuickSlots)과 현재 무기로 칸 아이콘을 채운다. 무기를 바꾸면 다시 부른다.
	void SetQuickSlots(const TArray<FQuickSlot>& QuickSlots, EWeaponType WeaponType);

	// 서버가 확정한 시전(SC_ATTACK)의 쿨타임을 그 스킬이 든 칸에 표시한다. SkillSlot = SkillTable의 Slot.
	void StartCooldown(int32 SkillSlot, int32 CooldownMs);
	// 그 스킬 칸의 쿨타임 표시를 남은 시간으로 다시 맞춘다(0 이하면 지움). 서버가 시전을 거부했을 때 쓴다 (CP1)
	void SetCooldownRemain(int32 SkillSlot, int32 RemainMs, int32 TotalMs);
	// 서버가 확정한 아이템 사용(SC_USE_ITEM)의 재사용 시간을 그 종류가 든 칸에 표시한다 (A3.6)
	void StartItemCooldown(int32 ItemId, int32 CooldownMs);
	// 그 아이템 칸의 재사용 표시를 남은 시간으로 맞춘다. 레벨 전환 뒤 새 HUD에 다시 넣을 때 쓴다 (CL1 리뷰 C2)
	void SetItemCooldownRemain(int32 ItemId, int32 RemainMs, int32 TotalMs);

	// 상호작용 안내(A6.2): 주울 수 있는 드랍이 가까이 있으면 "[F] 줍기: 이름". 빈 글이면 숨긴다.
	void SetInteractPrompt(const FText& Prompt);

	// 던전 상태 한 줄(DG3c-2): State = Protocol::DungeonState. 진행 중이면 "던전 진행 중", 클리어면 ClearEndSec(FPlatformTime::Seconds)까지
	// 남은 시간을 1초씩 줄여 보여 준다. NONE(던전 밖)이면 숨긴다. 값은 UJMObjectManager가 SC_DUNGEON_STATE·Room 변경 때 넘긴다.
	void SetDungeonState(int32 State, double ClearEndSec);

	// 사라지는 알림 줄(D-266, TxtNotice): 최근 NoticeMaxLines줄을 보여 주고 각 줄은 NoticeDurationSec 뒤 사라진다(마지막 0.5초 흐려짐).
	// 퀘스트 수락·목표 달성·완료처럼 확인이 필요 없는 안내용. UJMUIManager::ShowNotice가 부른다.
	void ShowNotice(const FText& Message);

	// 시스템 메뉴(UI8, D-277): 메뉴 버튼으로 열리는 작은 창. Esc·커서가 사라질 때 닫는다. 닫혀 있었으면 false
	bool CloseSystemMenu();

protected:
	// ---- 메뉴 버튼 줄(UI8, D-277) — 글자 버튼, 커서 모드(Ctrl)에서 누른다. 단축키 안내는 WBP 툴팁 ----
	UFUNCTION()
	void OnMenuInventoryClicked();
	UFUNCTION()
	void OnMenuQuestClicked();
	UFUNCTION()
	void OnMenuPartyClicked();
	UFUNCTION()
	void OnMenuAuctionClicked();
	UFUNCTION()
	void OnMenuSystemClicked();
	// 시스템 메뉴: 설정(아직 기능 없음 — 안내만), 로그인 화면으로, 게임 종료
	UFUNCTION()
	void OnSysSettingsClicked();
	UFUNCTION()
	void OnSysLoginClicked();
	UFUNCTION()
	void OnSysQuitClicked();

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnMenuInventory;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnMenuQuest;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnMenuParty;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnMenuAuction;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnMenuSystem;

	// 시스템 메뉴 창(처음엔 접힘)과 그 안의 버튼
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> SystemMenu;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnSysSettings;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnSysLogin;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnSysQuit;

	// HUD가 내 캐릭터보다 늦게 만들어져도 현재 값으로 시작한다.
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtName;

	// "Lv. N"
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtLevel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UProgressBar> HpBar;

	// "현재 / 최대"
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtHp;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UProgressBar> MpBar;

	// "현재 / 최대"
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtMp;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UProgressBar> ExpBar;

	// "현재 / 필요 (xx.x%)", 최대 레벨이면 "MAX"
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtExp;

	// 상호작용 안내 글(없으면 안내를 띄우지 않는다)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtInteract;

	// 던전 상태 한 줄(없으면 표시하지 않는다)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtDungeon;

	// 진행 중 퀘스트 줄(Q1 — "이름 7/15", 목표를 채우면 "(완료 가능)")
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtQuestTracker;

	// 수령함 알림(D-262 — 경매 판매·만료 뒤 받을 것이 남아 있는 동안)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtMail;

	// 사라지는 알림 줄(D-266)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtNotice;

	// 알림 한 줄이 떠 있는 시간(초)과 한 번에 보이는 줄 수
	UPROPERTY(EditAnywhere, Category = "Notice")
	float NoticeDurationSec = 3.f;
	UPROPERTY(EditAnywhere, Category = "Notice")
	int32 NoticeMaxLines = 3;

	// 액티브 칸 1~5(숫자키). 칸 번호는 각 인스턴스의 SkillSlot 값. 기본 공격은 좌클릭이라 칸을 두지 않는다. (D-038·D-148)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USkillSlotWidget> Slot1;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USkillSlotWidget> Slot2;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USkillSlotWidget> Slot3;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USkillSlotWidget> Slot4;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USkillSlotWidget> Slot5;

private:
	TArray<USkillSlotWidget*, TInlineAllocator<5>> GetSlotWidgets() const;

	// 인벤토리·액티브 칸 배치가 바뀌면(UJMInventoryManager::OnInventoryChanged) 내 캐릭터의 칸 내용을 다시 만들어 그린다(수량·배치).
	void OnInventoryChanged();
	// 칸에 인벤토리 아이템을 끌어다 놓음 = 그 종류를 올리기, 칸 우클릭 = 비우기 (CS_SET_QUICKSLOT, 결과는 SC_INVENTORY_UPDATE)
	void OnSlotItemDropped(USkillSlotWidget* SlotWidget, int32 ItemId);
	void OnSlotRightClicked(USkillSlotWidget* SlotWidget);

	FDelegateHandle InventoryChangedHandle;

	// 퀘스트 추적 줄·수령함 알림(Q1)
	struct FNoticeLine
	{
		FString Text;
		double EndSec = 0.0;   // FPlatformTime::Seconds 기준
	};
	TArray<FNoticeLine> Notices;
	void RefreshNotice();
	void RefreshQuestTracker();
	void RefreshMailNotice();
	FDelegateHandle QuestChangedHandle;
	FDelegateHandle AuctionChangedHandle;

	// 던전 상태 표시(SetDungeonState). ShownDungeonSec = 지금 글자에 적힌 남은 초(같으면 다시 쓰지 않는다)
	void RefreshDungeonText();
	int32 DungeonStateValue = 0;
	double DungeonClearEndSec = 0.0;
	int32 ShownDungeonSec = -1;

	// 마지막으로 받은 칸 내용(쿨타임을 어느 칸에 표시할지 찾는 데 쓴다)
	TArray<FQuickSlot> CurrentQuickSlots;
};
