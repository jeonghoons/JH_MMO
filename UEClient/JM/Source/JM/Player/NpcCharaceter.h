// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/JMCharacterBase.h"
#include "NpcCharaceter.generated.h"

/**
 * 
 */
UCLASS()
class JM_API ANpcCharaceter : public AJMCharacterBase
{
	GENERATED_BODY()
public:
	ANpcCharaceter();

	virtual void SetPlayerData(const Protocol::ObjectInfo& ObjInfo) override;
};
