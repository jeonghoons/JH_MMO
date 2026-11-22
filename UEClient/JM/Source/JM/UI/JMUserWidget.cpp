#include "UI/JMUserWidget.h"

#include "UI/UIStyle.h"

void UJMUserWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	JMUIStyle::ApplyTree(this);
}
