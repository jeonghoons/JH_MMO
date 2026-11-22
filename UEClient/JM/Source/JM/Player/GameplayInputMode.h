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

// 커서를 보이게 한다. 숨겨져 있다가 나타날 때는 항상 화면 가운데에서 나온다(사용자 요청 2026-11-23).
// 이미 보이는 중이면 위치를 건드리지 않는다(창을 하나 더 열어도 커서가 튀지 않게).
inline void ShowCursorCentered(APlayerController* PC)
{
	if (PC == nullptr) return;
	if (!PC->bShowMouseCursor)
	{
		int32 ViewX = 0, ViewY = 0;
		PC->GetViewportSize(ViewX, ViewY);
		if (ViewX > 0 && ViewY > 0) PC->SetMouseLocation(ViewX / 2, ViewY / 2);
	}
	PC->bShowMouseCursor = true;
}

// 자유 커서 모드: UI(인벤토리, 채팅 등)를 클릭할 때 쓴다. 게임 입력도 함께 받는다.
inline void ApplyFreeCursorInputMode(APlayerController* PC)
{
	if (PC == nullptr) return;

	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(InputMode);
	ShowCursorCentered(PC);
}