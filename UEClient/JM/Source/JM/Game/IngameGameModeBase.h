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

	UPROPERTY()
	TObjectPtr<class UUserWidget> HUDWidgetInstance;
};
