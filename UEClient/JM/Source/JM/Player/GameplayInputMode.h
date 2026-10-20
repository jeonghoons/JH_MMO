#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

// 전투 입력 모드(검은사막 방식): 마우스 커서를 숨기고 마우스로 카메라를 돌린다. 조준은 화면 중앙(크로스헤어) 기준이다.
inline void ApplyCombatInputMode(APlayerController* PC)
{
	if (PC == nullptr) return;

	PC->SetInputMode(FInputModeGameOnly());
	PC->bShowMouseCursor = false;
}

// 자유 커서 모드: UI(인벤토리, 채팅 등)를 클릭할 때 쓴다. 게임 입력도 함께 받는다.
inline void ApplyFreeCursorInputMode(APlayerController* PC)
{
	if (PC == nullptr) return;

	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(InputMode);
	PC->bShowMouseCursor = true;
}