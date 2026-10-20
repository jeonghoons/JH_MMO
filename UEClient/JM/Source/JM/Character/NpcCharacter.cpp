#include "Character/NpcCharacter.h"
#include "Components/WidgetComponent.h"
#include "UI/HpBarWidget.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ANpcCharacter::ANpcCharacter()
{
    HpBarComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("HpBarComponent"));

    HpBarComponent->SetupAttachment(GetMesh());
    HpBarComponent->SetWidgetSpace(EWidgetSpace::Screen);
    HpBarComponent->SetDrawSize(FVector2D(150.f, 20.f));
    HpBarComponent->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
}

void ANpcCharacter::SetPlayerData(const Protocol::ObjectInfo& ObjInfo)
{
	Super::SetPlayerData(ObjInfo);

    if (UHpBarWidget* HpWidget = Cast<UHpBarWidget>(HpBarComponent->GetUserWidgetObject()))
    {
        HpWidget->UpdateHp(ObjectInfo.stat().hp(), ObjectInfo.stat().max_hp());
    }
}

void ANpcCharacter::OnDamaged(int damage, int remainHp)
{
	Super::OnDamaged(damage, remainHp);

    if (UHpBarWidget* HpWidget = Cast<UHpBarWidget>(HpBarComponent->GetUserWidgetObject()))
    {
        HpWidget->UpdateHp(ObjectInfo.stat().hp(), ObjectInfo.stat().max_hp());
    }
}

void ANpcCharacter::OnDead()
{
	Super::OnDead();

	// 시체는 HP바를 숨기고 다른 캐릭터(Pawn)를 막지 않게 한다. 실제 제거는 서버의 SC_REMOVE_OBJECT로 이뤄진다.
	// 캡슐 충돌을 통째로 끄면 바닥 감지가 사라져 땅속으로 꺼지므로, Pawn 채널만 무시하고 월드 충돌은 유지한다.
	HpBarComponent->SetVisibility(false);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
}
