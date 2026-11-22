#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "UIStyleTableRow.generated.h"

class UFont;
class UTexture2D;

// Common/DataTable/UIStyleTable.csv 의 한 행(UE 전용 — 서버는 읽지 않는다, D-270). RowName = 스타일 키(Body·Title …).
// 글자 크기·색은 각 위젯(WBP·C++)이 정하고, 이 표는 어떤 글꼴을 쓸지만 정한다(UI3에서 색·테두리 키를 늘림).
USTRUCT(BlueprintType)
struct FUIStyleTableRow : public FTableRowBase
{
	GENERATED_BODY()

	// 글꼴 에셋(Composite Font, /Game/UI/Fonts)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UFont> Font;
	// 위젯이 쓰던 글자체 이름(Regular·Bold …)이 이 글꼴에 없을 때 쓸 글자체
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Typeface;
	// 색(sRGB). 브러시 행은 브러시 색, 색 행(Grade_* 등)은 그 색 자체 (UI3)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FColor Color = FColor::White;
	// 브러시 텍스처(/Game/UI/Frame). 비어 있으면 브러시 행이 아니다
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UTexture2D> Texture;
	// 9-slice 여백(텍스처 픽셀). 0이면 늘이기(Image)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MarginPx = 0.f;
};
