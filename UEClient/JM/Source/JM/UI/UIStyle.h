#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"

class UUserWidget;

// 공용 UI 스타일(D-270): 글꼴은 UIStyleTable(UDataManager)에서 읽는다. 표나 행이 없으면 아무것도 바꾸지 않는다.
namespace JMUIStyle
{
	JM_API extern const FName Body;		// 본문(Pretendard)
	JM_API extern const FName Title;	// 창 제목·이름(Noto Serif KR)

	// Info의 글꼴 객체·글자체만 Key 스타일의 글꼴로 바꾼다(크기·윤곽선·색은 그대로). 글자체는 같은 이름이 있으면 유지한다.
	JM_API void ApplyFont(const UObject* WorldContext, FSlateFontInfo& Info, FName Key);

	// 표의 색(sRGB → 선형). 행이 없으면 Fallback
	JM_API FLinearColor Color(const UObject* WorldContext, FName Key, const FLinearColor& Fallback = FLinearColor::White);
	// 아이템 등급 테두리 색(행 Grade_<등급 이름> — DESIGN_ART_CONCEPT §2, Epic은 D-270)
	JM_API FLinearColor GradeColor(const UObject* WorldContext, uint8 Grade);
	// 표의 브러시 행(Texture·MarginPx·Color)으로 Out을 채운다. 행·텍스처가 없으면 false(Out은 그대로)
	JM_API bool MakeBrush(const UObject* WorldContext, FName Key, FSlateBrush& Out);

	// 금액·수량 표시: 세 자리마다 쉼표(20,000). 로캘과 무관하게 같은 모양으로 만든다(AU2b, D-281)
	JM_API FString GoldText(int64 Value);

	// 위젯 트리에 공용 스타일을 입힌다(UJMUserWidget이 처음 만들어질 때 부름):
	//   글자(TextBlock·EditableText·EditableTextBox) = 글꼴(이름이 TxtTitle로 시작하면 Title, 나머지 Body)
	//   Border "Panel" = 창 바탕(Window), Border "SlotBg" = 칸 바탕(Slot), Button "Btn*" = 버튼(Button·ButtonHover) (UI3)
	// 트리 안의 다른 사용자 위젯은 각자 자기 트리를 적용한다.
	JM_API void ApplyTree(UUserWidget* Widget);
}
