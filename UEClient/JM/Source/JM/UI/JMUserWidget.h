#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "JMUserWidget.generated.h"

// 프로젝트 UI 위젯의 공통 부모(D-270): 처음 만들어질 때 트리의 글자에 공용 글꼴(UIStyleTable)을 입힌다.
// 위젯 컴포넌트로 만든 위젯(이름표)은 NativeOnInitialized가 불리지 않으므로 직접 JMUIStyle을 부른다.
UCLASS(Abstract)
class JM_API UJMUserWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeOnInitialized() override;
};
