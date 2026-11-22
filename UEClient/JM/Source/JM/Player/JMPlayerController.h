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

	// Hold Left Alt: temporarily show the mouse cursor to click UI (combat mode hides it). Release restores the mode.
	void OnFreeCursorPressed();
	void OnFreeCursorReleased();

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
    //   "AuctionSearch [slot] [page]"          requests a search page (slot 0 = all, 1-7 = equip slot, 8 = other).
    //   "AuctionMy"                            requests my listings + mailbox.
    //   "AuctionRegister <itemId> <count> <price>"  registers an item of that kind from my bag (NPC mode).
    //   "AuctionBuy <listingId>"               buys a listing (0 = the first listing in the current page that is not mine).
    //   "AuctionCancel <listingId>"            cancels my listing (0 = my first listing).
    //   "MailClaim <mailId>"                   claims a mail (0 = the first mail).
    //   "AuctionShow"                          logs the search page, my listings and the mailbox.
    //   "AuctionTab <0-3>"                     switches the window tab (0 search, 1 register, 2 my listings, 3 mailbox).
    //   "AuctionClose"                         closes the window.
    // Distance to the NPC, gold, limits and listing state are validated by the server.
    UFUNCTION(Exec)
    void AuctionOpen(int32 AtNpc = 0);
    UFUNCTION(Exec)
    void AuctionSearch(int32 Slot = 0, int32 Page = 0);
    UFUNCTION(Exec)
    void AuctionMy();
    UFUNCTION(Exec)
    void AuctionRegister(int32 ItemId, int32 Count = 1, int32 Price = 1);
    UFUNCTION(Exec)
    void AuctionBuy(int32 ListingId = 0);
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

protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
    TSubclassOf<class UUserWidget> ChatWidgetClass;

    // Crosshair shown at the screen center. Set this in BP_PControler (e.g. WBP_Crosshair).
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
    TSubclassOf<class UUserWidget> CrosshairWidgetClass;
};
