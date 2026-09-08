#include "Player/NpcCharaceter.h"
#include "Components/WidgetComponent.h"
#include "UI/HpBarWidget.h"

ANpcCharaceter::ANpcCharaceter()
{
    HpBarComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("HpBarComponent"));

    HpBarComponent->SetupAttachment(GetMesh());
    HpBarComponent->SetWidgetSpace(EWidgetSpace::Screen);
    HpBarComponent->SetDrawSize(FVector2D(150.f, 20.f));
    HpBarComponent->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
}

void ANpcCharaceter::SetPlayerData(const Protocol::ObjectInfo& ObjInfo)
{
	Super::SetPlayerData(ObjInfo);

    if (UHpBarWidget* HpWidget = Cast<UHpBarWidget>(HpBarComponent->GetUserWidgetObject()))
    {
        HpWidget->UpdateHp(ObjectInfo.stat().hp(), ObjectInfo.stat().max_hp());
    }
}

void ANpcCharaceter::OnDamaged(int damage, int remainHp)
{
	Super::OnDamaged(damage, remainHp);

    if (UHpBarWidget* HpWidget = Cast<UHpBarWidget>(HpBarComponent->GetUserWidgetObject()))
    {
        HpWidget->UpdateHp(ObjectInfo.stat().hp(), ObjectInfo.stat().max_hp());
    }
}
