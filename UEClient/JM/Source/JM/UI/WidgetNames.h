#pragma once

#include "CoreMinimal.h"

// UJMUIManager에 등록하는 위젯 이름. 문자열을 직접 쓰지 말고 이 상수를 사용한다.
namespace WidgetNames
{
	inline const FName Login(TEXT("Login"));
	inline const FName HUD(TEXT("HUD"));
	inline const FName Crosshair(TEXT("Crosshair"));
	inline const FName Chatting(TEXT("Chatting"));
	inline const FName SystemMessage(TEXT("SystemMessage"));
	inline const FName Revive(TEXT("Revive"));
	inline const FName Inventory(TEXT("Inventory"));
	inline const FName Trade(TEXT("Trade"));
	inline const FName Shop(TEXT("Shop"));
	inline const FName Party(TEXT("Party"));
	inline const FName Auction(TEXT("Auction"));
	inline const FName Quest(TEXT("Quest"));
}
