#include "Game/DropItemActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Data/ItemTableRow.h"
#include "Game/DataManager.h"

ADropItemActor::ADropItemActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	// 표시 전용: 캐릭터·카메라·조준 레이를 막지 않는다.
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
}

void ADropItemActor::InitDrop(int32 InObjectId, int32 InItemId, int64 InCount)
{
	ObjectId = InObjectId;
	ItemId = InItemId;
	Count = InCount;

	const bool bGold = IsGold();
	if (UStaticMesh* Shape = bGold ? GoldMesh.Get() : ItemMesh.Get())
	{
		Mesh->SetStaticMesh(Shape);
	}
	if (UMaterialInterface* Material = bGold ? GoldMaterial.Get() : ItemMaterial.Get())
	{
		Mesh->SetMaterial(0, Material);
	}
	Mesh->SetWorldScale3D(bGold ? GoldScale : ItemScale);

	// 서버 z는 내비메시 높이라 실제 바닥과 조금 다를 수 있다. 그 둘레에서 바닥을 찾아 그 위에 띄운다(못 찾으면 서버 z 기준).
	FVector Location = GetActorLocation();
	if (UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DropItemGround), false, this);
		if (World->LineTraceSingleByChannel(Hit, Location + FVector(0.f, 0.f, 150.f), Location - FVector(0.f, 0.f, 150.f), ECC_WorldStatic, Params))
		{
			Location.Z = Hit.ImpactPoint.Z;
		}
	}
	Location.Z += HoverHeight;
	SetActorLocation(Location);
}

FText ADropItemActor::GetDisplayText() const
{
	if (IsGold())
	{
		return FText::FromString(FString::Printf(TEXT("골드 %lld"), Count));
	}

	FString Name = FString::FromInt(ItemId);
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UDataManager* DataManager = GameInstance->GetSubsystem<UDataManager>())
		{
			if (const FItemTableRow* Row = DataManager->FindItemById(ItemId))
			{
				Name = Row->DisplayName.ToString();
			}
		}
	}
	return FText::FromString(Count >= 2 ? FString::Printf(TEXT("%s x%lld"), *Name, Count) : Name);
}

void ADropItemActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (SpinDegPerSec != 0.f)
	{
		AddActorLocalRotation(FRotator(0.f, SpinDegPerSec * DeltaTime, 0.f));
	}
}
