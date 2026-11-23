#include "Character/NpcCharacter.h"
#include "Components/WidgetComponent.h"
#include "UI/HpBarWidget.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Game/DataManager.h"
#include "Engine/SkeletalMesh.h"

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

	if (!bAppearanceApplied)
	{
		bAppearanceApplied = true;
		ApplyMonsterAppearance(ObjInfo.template_id());
		// 스폰 위치는 서버 z(지면)라 크기를 입힌 뒤 캡슐 중심 높이로 올린다
		SetActorLocation(FVector(ObjInfo.position().x(), ObjInfo.position().y(), ObjInfo.position().z() + GetServerZOffset()));
	}

    if (UHpBarWidget* HpWidget = Cast<UHpBarWidget>(HpBarComponent->GetUserWidgetObject()))
    {
        HpWidget->UpdateHp(ObjectInfo.stat().hp(), ObjectInfo.stat().max_hp());
    }
}

void ANpcCharacter::ApplyMonsterAppearance(int32 TemplateId)
{
	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FMonsterTableRow* Row = DataManager ? DataManager->FindMonster(TemplateId) : nullptr;
	if (Row == nullptr)
	{
		// 행이 없으면 BP_Npc의 기본 메시·크기 그대로 보인다(서버 MonsterTable과 DT_MonsterTable이 어긋난 경우).
		UE_LOG(LogTemp, Warning, TEXT("NpcCharacter: MonsterTable has no row %d - default appearance"), TemplateId);
		return;
	}

	// 스킨: 같은 스켈레톤의 메시로 바꾼다(AnimBP는 그대로). 방 입장 전에 UDataManager::PreloadGameAssets가 미리 로드해 둔다.
	if (USkeletalMesh* SkinMesh = Row->Mesh.LoadSynchronous())
	{
		if (GetMesh()->GetSkeletalMeshAsset() != SkinMesh)
			GetMesh()->SetSkeletalMeshAsset(SkinMesh);
	}

	// 크기: 액터 전체에 곱한다(캡슐·메시·HP 막대 위치가 함께 변한다). 서버 판정은 같은 행의 BodyRadiusCm를 쓴다.
	if (Row->Scale > 0.f && !FMath::IsNearlyEqual(Row->Scale, 1.f))
		SetActorScale3D(FVector(Row->Scale));
}

float ANpcCharacter::GetServerZOffset() const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	return Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.f;
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
