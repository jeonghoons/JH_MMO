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

    // Dev console command (MC4.1b): "NetMoveStats [reset 0/1]" logs remote-player move display stats
    // (screen speed / move stat, packet-arrival position error). Default resets after printing.
    UFUNCTION(Exec)
    void NetMoveStats(int32 Reset = 1);

protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
    TSubclassOf<class UUserWidget> ChatWidgetClass;

    // Crosshair shown at the screen center. Set this in BP_PControler (e.g. WBP_Crosshair).
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
    TSubclassOf<class UUserWidget> CrosshairWidgetClass;
};
