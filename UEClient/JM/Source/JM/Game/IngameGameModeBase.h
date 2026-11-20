#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "IngameGameModeBase.generated.h"

UCLASS()
class JM_API AIngameGameModeBase : public AGameModeBase
{
	GENERATED_BODY()
public:
	AIngameGameModeBase();

protected:
	virtual void BeginPlay() override;

protected:
	UPROPERTY(EditAnywhere, Category = "UI")
	TSubclassOf<class UUserWidget> HUDWidgetClass;

	// Revive window (WBP_Revive, parent UReviveWidget). Shown while my character is dead (A2.4).
	UPROPERTY(EditAnywhere, Category = "UI")
	TSubclassOf<class UUserWidget> ReviveWidgetClass;

	// Inventory + equipment window (WBP_Inventory, parent UInventoryWidget). Toggled with IA_Inventory (A6.1).
	UPROPERTY(EditAnywhere, Category = "UI")
	TSubclassOf<class UUserWidget> InventoryWidgetClass;

	// 1:1 trade window (WBP_Trade, parent UTradeWidget). Shown and hidden by UJMTradeManager (A6.3a).
	UPROPERTY(EditAnywhere, Category = "UI")
	TSubclassOf<class UUserWidget> TradeWidgetClass;

	// Shop NPC window (WBP_Shop, parent UShopWidget). Shown and hidden by UJMShopManager (L4).
	UPROPERTY(EditAnywhere, Category = "UI")
	TSubclassOf<class UUserWidget> ShopWidgetClass;

	// Party window (WBP_Party, parent UPartyWidget). Toggled with IA_Party, also shown by UJMPartyManager on invite (DG2).
	UPROPERTY(EditAnywhere, Category = "UI")
	TSubclassOf<class UUserWidget> PartyWidgetClass;
};
