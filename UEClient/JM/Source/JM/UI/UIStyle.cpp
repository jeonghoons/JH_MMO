#include "UI/UIStyle.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Data/ItemTableRow.h"
#include "Data/UIStyleTableRow.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Game/DataManager.h"

namespace JMUIStyle
{
	const FName Body(TEXT("Body"));
	const FName Title(TEXT("Title"));

	static const FUIStyleTableRow* FindRow(const UObject* WorldContext, FName Key)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		const UDataManager* DataManager = GameInstance ? GameInstance->GetSubsystem<UDataManager>() : nullptr;
		return DataManager ? DataManager->FindUIStyle(Key) : nullptr;
	}

	static bool HasTypeface(const UFont* Font, FName Name)
	{
		if (Font == nullptr || Name.IsNone()) return false;
		for (const FTypefaceEntry& Entry : Font->GetCompositeFont()->DefaultTypeface.Fonts)
		{
			if (Entry.Name == Name) return true;
		}
		return false;
	}

	void ApplyFont(const UObject* WorldContext, FSlateFontInfo& Info, FName Key)
	{
		const FUIStyleTableRow* Row = FindRow(WorldContext, Key);
		UFont* Font = Row ? Row->Font.LoadSynchronous() : nullptr;
		if (Font == nullptr) return;

		Info.FontObject = Font;
		if (!HasTypeface(Font, Info.TypefaceFontName)) Info.TypefaceFontName = Row->Typeface;
	}

	FLinearColor Color(const UObject* WorldContext, FName Key, const FLinearColor& Fallback)
	{
		const FUIStyleTableRow* Row = FindRow(WorldContext, Key);
		return Row ? FLinearColor(Row->Color) : Fallback;
	}

	FLinearColor GradeColor(const UObject* WorldContext, uint8 Grade)
	{
		const FString Name = StaticEnum<EItemGrade>()->GetNameStringByValue(Grade);
		return Color(WorldContext, FName(*FString::Printf(TEXT("Grade_%s"), *Name)), FLinearColor(0.34f, 0.34f, 0.34f));
	}

	FString GoldText(int64 Value)
	{
		FString Digits = FString::Printf(TEXT("%lld"), Value < 0 ? -Value : Value);
		for (int32 Pos = Digits.Len() - 3; Pos > 0; Pos -= 3) Digits.InsertAt(Pos, TEXT(","));
		return Value < 0 ? TEXT("-") + Digits : Digits;
	}

	bool MakeBrush(const UObject* WorldContext, FName Key, FSlateBrush& Out)
	{
		const FUIStyleTableRow* Row = FindRow(WorldContext, Key);
		UTexture2D* Texture = Row ? Row->Texture.LoadSynchronous() : nullptr;
		if (Texture == nullptr) return false;

		Out.SetResourceObject(Texture);
		Out.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		Out.TintColor = FSlateColor(FLinearColor(Row->Color));
		if (Row->MarginPx > 0.f)
		{
			// 9-slice: 여백은 텍스처 크기에 대한 비율
			Out.DrawAs = ESlateBrushDrawType::Box;
			Out.Margin = FMargin(Row->MarginPx / Out.ImageSize.X, Row->MarginPx / Out.ImageSize.Y);
		}
		else
		{
			Out.DrawAs = ESlateBrushDrawType::Image;
		}
		return true;
	}

	static void ApplyButton(const UObject* WorldContext, UButton* Button)
	{
		FSlateBrush Normal, Hover;
		if (!MakeBrush(WorldContext, TEXT("Button"), Normal)) return;
		if (!MakeBrush(WorldContext, TEXT("ButtonHover"), Hover)) Hover = Normal;
		FButtonStyle Style = Button->GetStyle();
		Style.SetNormal(Normal);
		Style.SetHovered(Hover);
		FSlateBrush Pressed = Hover;
		Pressed.TintColor = FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f));
		Style.SetPressed(Pressed);
		FSlateBrush Disabled = Normal;
		Disabled.TintColor = FSlateColor(FLinearColor(0.45f, 0.45f, 0.45f, 0.8f));
		Style.SetDisabled(Disabled);
		Button->SetStyle(Style);
		// 버튼 위 커서 = 클릭 가능(UI2 하드웨어 커서 Hand, D-274)
		Button->SetCursor(EMouseCursor::Hand);
		// 어두운 버튼 바탕 위 글자는 밝게(ButtonText 행)
		if (UTextBlock* Label = Cast<UTextBlock>(Button->GetContent()))
			Label->SetColorAndOpacity(FSlateColor(Color(WorldContext, TEXT("ButtonText"), FLinearColor(0.87f, 0.78f, 0.55f))));
	}

	void ApplyTree(UUserWidget* Widget)
	{
		if (Widget == nullptr || Widget->WidgetTree == nullptr) return;
		Widget->WidgetTree->ForEachWidget([Widget](UWidget* Child)
		{
			const FString Name = Child->GetName();
			const FName Key = Name.StartsWith(TEXT("TxtTitle")) ? Title : Body;
			if (UTextBlock* Text = Cast<UTextBlock>(Child))
			{
				FSlateFontInfo Info = Text->GetFont();
				ApplyFont(Widget, Info, Key);
				Text->SetFont(Info);
			}
			else if (UEditableText* Edit = Cast<UEditableText>(Child))
			{
				FSlateFontInfo Info = Edit->GetFont();
				ApplyFont(Widget, Info, Key);
				Edit->SetFont(Info);
			}
			else if (UEditableTextBox* Box = Cast<UEditableTextBox>(Child))
			{
				FEditableTextBoxStyle Style = Box->GetWidgetStyle();
				ApplyFont(Widget, Style.TextStyle.Font, Key);
				Box->SetWidgetStyle(Style);
			}
			else if (UBorder* Border = Cast<UBorder>(Child))
			{
				const FName BrushKey = Name == TEXT("Panel") ? FName(TEXT("Window")) : Name == TEXT("SlotBg") ? FName(TEXT("Slot")) : NAME_None;
				FSlateBrush Brush;
				if (!BrushKey.IsNone() && MakeBrush(Widget, BrushKey, Brush))
				{
					Border->SetBrush(Brush);
					// 칸 테두리는 흰 텍스처에 색을 곱한다 — 빈 칸 색(Slot_Empty), 아이템 칸은 등급 색으로 다시 칠함(UItemSlotWidget)
					Border->SetBrushColor(BrushKey == TEXT("Slot") ? Color(Widget, TEXT("Slot_Empty")) : FLinearColor::White);
				}
			}
			else if (UButton* Button = Cast<UButton>(Child))
			{
				if (Name.StartsWith(TEXT("Btn"))) ApplyButton(Widget, Button);
			}
		});
	}
}
