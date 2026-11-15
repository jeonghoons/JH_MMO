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
};
