#include "Character/NpcCharacter.h"
#include "Components/WidgetComponent.h"
#include "UI/HpBarWidget.h"
#include "Blueprint/UserWidget.h"
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
    // 기본은 숨김 — 나·파티원이 때렸을 때만 보인다(FP1, D-286). 화면 공간 위젯 컴포넌트는 숨긴 채 시작하면 나중에 켜도
    // 화면 층에 붙지 않아, 컴포넌트는 켜 두고 위젯 자체의 표시를 바꾼다(ApplyHpBarVisibility).
}

void ANpcCharacter::RefreshHpBar()
{
	if (UHpBarWidget* HpWidget = Cast<UHpBarWidget>(HpBarComponent->GetUserWidgetObject()))
		HpWidget->UpdateHp(ObjectInfo.stat().hp(), ObjectInfo.stat().max_hp());
}

void ANpcCharacter::ApplyHpBarVisibility()
{
	// 위젯은 컴포넌트 등록 뒤에 생기므로 생길 때까지 매 Tick 맞춘다
	if (UUserWidget* Widget = HpBarComponent->GetUserWidgetObject())
	{
		const ESlateVisibility Want = bHpBarShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
		if (Widget->GetVisibility() != Want) Widget->SetVisibility(Want);
	}
}

void ANpcCharacter::SetHpBarShown(bool bShow)
{
	if (bShow && ObjectInfo.stat().hp() <= 0) bShow = false;   // 죽은 몬스터는 다시 보이지 않는다
	if (bHpBarShown == bShow) return;
	bHpBarShown = bShow;
	if (bShow) RefreshHpBar();
	ApplyHpBarVisibility();
}

void ANpcCharacter::OnHitByAlly()
{
	LastAllyHitTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	// 이 피해로 HP가 가득 찰 수는 없지만, 막 죽었으면 SetHpBarShown이 거른다
	SetHpBarShown(true);
}

void ANpcCharacter::SetTargetIsAlly(bool bAlly)
{
	// 대상이 우리 편에서 다른 사람·없음(복귀)으로 바뀌는 순간 숨긴다. 원래 남을 노리던 몬스터를 내가 때린 경우는 시간으로 숨긴다
	if (bTargetIsAlly && !bAlly) SetHpBarShown(false);
	bTargetIsAlly = bAlly;
}

void ANpcCharacter::SetServerHp(int32 Hp)
{
	Super::SetServerHp(Hp);
	RefreshHpBar();
	// 복귀하며 HP를 다 채웠으면(ReturnMode 1) 숨긴다
	if (Hp >= ObjectInfo.stat().max_hp()) SetHpBarShown(false);
}

void ANpcCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bHpBarShown && !bTargetIsAlly && GetWorld() && GetWorld()->GetTimeSeconds() - LastAllyHitTime > HpBarShowSec)
		SetHpBarShown(false);
	ApplyHpBarVisibility();
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
	SetHpBarShown(false);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
}
