// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/JMPlayerController.h"

void AJMPlayerController::BeginPlay()
{
	Super::BeginPlay();
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
}
