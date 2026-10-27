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
    void OnInventoryKeyPressed();

    UFUNCTION(BlueprintCallable, Category = "UI")
    void OnChatKeyPressed();

protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
    TSubclassOf<class UUserWidget> ChatWidgetClass;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
    TSubclassOf<class UUserWidget> InventoryWidgetClass;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
    TSubclassOf<class UUserWidget> InfoWidgetClass;

    // Crosshair shown at the screen center. Set this in BP_PControler (e.g. WBP_Crosshair).
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
    TSubclassOf<class UUserWidget> CrosshairWidgetClass;
};
