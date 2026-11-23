// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "JMPlayerController.generated.h"

/**
 * 
 */
UCLASS()
class JM_API AJMPlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	// 조작(UI6, D-274): Ctrl = 커서 모드 켜고 끄기(UJMUIManager), Left Alt를 누르는 동안 = 둘러보기(카메라만 돌고 이동 방향 고정),
	// Esc = 플레이어 메뉴 닫기.
	void OnCursorModeToggle();
	void OnFreeLookPressed();
	void OnFreeLookReleased();
	void OnEscapePressed();

public:
    UFUNCTION(BlueprintCallable, Category = "UI")
    void OnChatKeyPressed();

    // Dev console command (MC3.2, A3.3): "Equip <part 1~7> <itemId>" (itemId 0 = unequip).
    // Finds an item of that kind in my inventory and sends CS_CHANGE_EQUIP with its item_db_id.
    // The server validates and replies. Kept as a dev command even after WBP_Equipment exists (D-160).
    UFUNCTION(Exec)
    void Equip(int32 Part, int32 ItemId);

    // Dev console command (A3.3): "Give <itemId> <count>" sends CS_DEV_GIVE (itemId 0 = gold).
    // Works only when the server runs with [Debug] DevCommands=true.
    UFUNCTION(Exec)
    void Give(int32 ItemId, int32 Count = 1);

    // Dev console commands (L4 shop): the same requests the shop window sends, for checks without mouse input.
    //   "ShopOpen"                 asks the nearest shop NPC in view for its item list (the window opens on SC_SHOP_OPEN).
    //   "ShopBuy <itemId> <count>"  buys from the open shop.
    //   "ShopSell <itemId> <count>" picks an item of that kind in my bag as the sell candidate and sells it.
    //   "ShopClose"                closes the window.
    // Distance, trade state, gold and free slots are validated by the server.
    UFUNCTION(Exec)
    void ShopOpen();
    UFUNCTION(Exec)
    void ShopBuy(int32 ItemId, int32 Count = 1);
    UFUNCTION(Exec)
    void ShopSell(int32 ItemId, int32 Count = 1);
    UFUNCTION(Exec)
    void ShopClose();

    // Dev console commands (A6.3b auction): the same requests the auction window sends, for checks without mouse input.
    //   "AuctionOpen [npc 0|1]"                opens the window: 0 = browse mode (K key), 1 = at the nearest auction NPC in view (F key).
    //   "AuctionSearch [keyword] [sort] [desc]" searches by item name (empty = all). sort 0 unit price, 1 total, 2 remaining time (-1 = keep), desc 0|1. (AU2b)
    //   "AuctionSort <sort> [desc]"            changes the sort of the current search.
    //   "AuctionPage <page>"                   moves to a page (1-based); past the cached rows it requests the next batch.
    //   "AuctionMy"                            requests my listings + mailbox.
    //   "AuctionRegister <itemId> <count> <unitPrice>"  registers an item of that kind from my bag (NPC mode).
    //   "AuctionBuy <listingId> [count]"       buys count from a listing (0 = the first listing in the current page that is not mine).
    //   "AuctionCancel <listingId>"            cancels my listing (0 = my first listing).
    //   "MailClaim <mailId>"                   claims a mail (0 = the first mail).
    //   "AuctionShow"                          logs the search page, my listings and the mailbox.
    //   "AuctionTab <0-3>"                     switches the window tab (0 search, 1 register, 2 my listings, 3 mailbox).
    //   "AuctionClose"                         closes the window.
    // Distance to the NPC, gold, limits and listing state are validated by the server.
    UFUNCTION(Exec)
    void AuctionOpen(int32 AtNpc = 0);
    UFUNCTION(Exec)
    void AuctionSearch(const FString& Keyword = TEXT(""), int32 Sort = -1, int32 Desc = 0);
    UFUNCTION(Exec)
    void AuctionSort(int32 Sort = 0, int32 Desc = 0);
    UFUNCTION(Exec)
    void AuctionPage(int32 Page = 1);
    UFUNCTION(Exec)
    void AuctionMy();
    UFUNCTION(Exec)
    void AuctionRegister(int32 ItemId, int32 Count = 1, int32 UnitPrice = 1);
    UFUNCTION(Exec)
    void AuctionBuy(int32 ListingId = 0, int32 Count = 1);
    UFUNCTION(Exec)
    void AuctionCancel(int32 ListingId = 0);
    UFUNCTION(Exec)
    void MailClaim(int32 MailId = 0);
    UFUNCTION(Exec)
    void AuctionShow();
    UFUNCTION(Exec)
    void AuctionTab(int32 TabIndex = 0);
    UFUNCTION(Exec)
    void AuctionClose();

    // Dev console commands (Q1 quests): the same requests the quest window / F key / L key send, for checks without mouse input.
    //   "QuestTalk"              opens the talk window at the nearest quest NPC in view (F key; the server checks the distance on request).
    //   "QuestRequest <questId>" sends CS_QUEST_REQUEST to the nearest quest NPC in view (the server accepts or completes by state).
    //   "QuestLog"               opens the quest log (L key).
    //   "QuestShow"              logs my quests (state, progress, ready to complete).
    //   "QuestClose"             closes the quest window.
    UFUNCTION(Exec)
    void QuestTalk();
    UFUNCTION(Exec)
    void QuestRequest(int32 QuestId);
    UFUNCTION(Exec)
    void QuestLog();
    UFUNCTION(Exec)
    void QuestShow();
    UFUNCTION(Exec)
    void QuestClose();

    // Dev console command (DG3b): "Portal [id]" sends the portal use request for that id, or for the nearest portal in range when id is 0.
    // Range, cast/move locks and the portal's destination are checked by the server (a rejection gets no reply).
    UFUNCTION(Exec)
    void Portal(int32 PortalId = 0);

    // Dev console commands (DG2 party): the same requests the party window / G key / chat command send, for checks without mouse input.
    //   "PartyInvite <name>"     invites the character with that name (any room in my channel).
    //   "PartyRespond <0|1>"     declines / accepts the pending invite.
    //   "PartyLeave"             leaves my party.
    //   "PartyShow"              logs my party (leader, members, where they are) and the pending invite.
    UFUNCTION(Exec)
    void PartyInvite(const FString& Name);
    UFUNCTION(Exec)
    void PartyRespond(int32 Accept = 1);
    UFUNCTION(Exec)
    void PartyLeave();
    UFUNCTION(Exec)
    void PartyShow();

    // Dev console command (MC4.1b): "NetMoveStats [reset 0/1]" logs remote-player move display stats
    // (screen speed / move stat, packet-arrival position error). Default resets after printing.
    UFUNCTION(Exec)
    void NetMoveStats(int32 Reset = 1);

    // Dev console commands (CP1): "AutoAttack <count> [slot]" casts the slot on the first frame it becomes castable, count times
    // (measures near-miss rejects under jm.NetLagMs / jm.NetLagJitterMs). "AttackStats [reset 0/1]" logs sent/confirmed/rejected counts.
    UFUNCTION(Exec)
    void AutoAttack(int32 Count = 30, int32 Slot = 0);
    UFUNCTION(Exec)
    void AttackStats(int32 Reset = 1);
    // 등록된 창을 열고 닫는다(WidgetNames — Inventory·Party·Quest …). UI 확인용(D-270, 키 입력 없이 PIE 확인)
    UFUNCTION(Exec)
    void UIToggle(const FString& WidgetName);

    // 개발용(UI6, D-274 — 마우스 없이 PIE 확인): "CursorMode <0|1>" = Ctrl과 같음, "FreeLook <0|1>" = Alt 누름/뗌,
    // "RClick <x> <y>" / "LClick <x> <y>" = 커서 모드에서 화면 좌표(뷰포트 픽셀)를 우클릭/좌클릭한 것과 같은 처리(x·y < 0이면 화면 가운데),
    // "RClickObject <id> [portal 0|1]" = 그 NPC·플레이어(portal 1이면 그 포털)를 화면에 투영한 자리를 우클릭,"PlayerMenu <trade|party|close>" = 메뉴 버튼, "InteractShow" = 고르기·자동 걷기 상태를 로그로
    UFUNCTION(Exec)
    void CursorMode(int32 Enable = 1);
    UFUNCTION(Exec)
    void FreeLook(int32 Enable = 1);
    UFUNCTION(Exec)
    void RClick(float X = -1.f, float Y = -1.f);
    UFUNCTION(Exec)
    void LClick(float X = -1.f, float Y = -1.f);
    UFUNCTION(Exec)
    void RClickObject(int32 ObjectId, int32 bPortal = 0);
    UFUNCTION(Exec)
    void PlayerMenu(const FString& Action);
    UFUNCTION(Exec)
    void InteractShow();

protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
    TSubclassOf<class UUserWidget> ChatWidgetClass;

    // Crosshair shown at the screen center. Set this in BP_PControler (e.g. WBP_Crosshair).
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
    TSubclassOf<class UUserWidget> CrosshairWidgetClass;
};
