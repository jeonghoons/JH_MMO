#include "Game/DropItemActor.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Data/ItemTableRow.h"
#include "Game/DataManager.h"
#include "UI/UIStyle.h"

namespace
{
	// 바닥 찾기: 서버 위치(내비메시 — 지면보다 평균 15cm 높음) 위아래로 이만큼
	constexpr float GroundTraceUpCm = 150.f;
	constexpr float GroundTraceDownCm = 300.f;
	// 등급 원판 두께 배율(원기둥 100cm 기준)과 바닥에서 띄우는 높이(지면과 겹쳐 깜박이지 않게)
	constexpr float RingThicknessScale = 0.01f;
	constexpr float RingLiftCm = 0.6f;
	constexpr float MeshLiftCm = 1.f;
}

ADropItemActor::ADropItemActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// 표시 전용: 캐릭터·카메라·조준 레이를 막지 않는다.
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);

	GradeRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GradeRing"));
	GradeRing->SetupAttachment(Root);
	GradeRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GradeRing->SetGenerateOverlapEvents(false);
	GradeRing->SetCanEverAffectNavigation(false);
	GradeRing->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> IconMatFinder(TEXT("/Game/_Core/Drop/M_DropIcon.M_DropIcon"));
	IconPlaneMesh = PlaneFinder.Object;
	RingMesh = CylinderFinder.Object;
	RingMaterial = ShapeMatFinder.Object;
	IconMaterial = IconMatFinder.Object;
}

float ADropItemActor::TraceGroundZ(const FVector& At) const
{
	UWorld* World = GetWorld();
	if (World == nullptr) return At.Z;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DropItemGround), false, this);
	// 오브젝트 종류로 고정 지형만 본다 — 채널(ECC_WorldStatic) 트레이스는 이 채널을 막는 캐릭터 캡슐(죽은 몬스터 포함)에도 맞아 공중에 떴다(FP1)
	if (World->LineTraceSingleByObjectType(Hit, At + FVector(0.f, 0.f, GroundTraceUpCm), At - FVector(0.f, 0.f, GroundTraceDownCm),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		return Hit.ImpactPoint.Z;
	}
	return At.Z;
}

void ADropItemActor::InitDrop(int32 InObjectId, int32 InItemId, int64 InCount)
{
	ObjectId = InObjectId;
	ItemId = InItemId;
	Count = InCount;

	const FVector ServerLoc = GetActorLocation();
	GroundZ = TraceGroundZ(ServerLoc);
	SetActorLocation(FVector(ServerLoc.X, ServerLoc.Y, GroundZ));
	// 같은 드랍은 어느 창에서 봐도 같은 방향으로 놓이게 id로 돌린다
	const float Yaw = static_cast<float>((ObjectId * 137) % 360);

	const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FItemTableRow* Row = (!IsGold() && DataManager) ? DataManager->FindItemById(ItemId) : nullptr;

	if (IsGold())
	{
		if (GoldMesh) Mesh->SetStaticMesh(GoldMesh);
		if (GoldMaterial) Mesh->SetMaterial(0, GoldMaterial);
		Mesh->SetRelativeScale3D(GoldScale);
		GradeRing->SetVisibility(false);
		PlaceMeshOnGround();
		return;
	}

	UStaticMesh* ItemMesh = (Row && !Row->StaticMesh.IsNull()) ? Row->StaticMesh.LoadSynchronous() : nullptr;
	if (ItemMesh)
	{
		// 자체 메시: 투구는 세운 채, 무기·방패는 눕힌다(정규화 메시 — 검·활 긴 축 +Z, 방패 앞면 +X → 피치 90이면 칼이 눕고 방패 앞면이 위)
		Mesh->SetStaticMesh(ItemMesh);
		const bool bHead = Row->Slot == EEquipSlot::Head;
		Mesh->SetRelativeRotation(bHead ? FRotator(0.f, Yaw, 0.f) : FRotator(90.f, Yaw, 0.f));
		const FVector Size = ItemMesh->GetBounds().BoxExtent * 2.f;
		const float MaxLen = FMath::Max3(Size.X, Size.Y, Size.Z);
		const float Target = bHead ? HelmMaxLenCm : MeshMaxLenCm;
		Mesh->SetRelativeScale3D(FVector(MaxLen > 0.f ? Target / MaxLen : 1.f));
		PlaceMeshOnGround();
	}
	else if (IconPlaneMesh && IconMaterial)
	{
		// 아이콘 판: 카메라 쪽으로 세운다(매 Tick yaw만 맞춤)
		bIcon = true;
		Mesh->SetStaticMesh(IconPlaneMesh);
		Mesh->SetCastShadow(false);
		if (UMaterialInstanceDynamic* MID = Mesh->CreateDynamicMaterialInstance(0, IconMaterial))
		{
			if (UTexture2D* Icon = Row && !Row->Icon.IsNull() ? Row->Icon.LoadSynchronous() : nullptr)
				MID->SetTextureParameterValue(TEXT("Icon"), Icon);
		}
		Mesh->SetRelativeScale3D(FVector(IconSizeCm / 100.f));   // 엔진 평면은 100cm
		Mesh->SetRelativeLocation(FVector(0.f, 0.f, IconHeightCm));
		FaceIconToCamera();
	}

	// 등급 색 원판
	if (RingMesh && RingMaterial)
	{
		GradeRing->SetStaticMesh(RingMesh);
		GradeRing->SetRelativeScale3D(FVector(RingDiameterCm / 100.f, RingDiameterCm / 100.f, RingThicknessScale));
		GradeRing->SetRelativeLocation(FVector(0.f, 0.f, RingLiftCm));
		if (UMaterialInstanceDynamic* MID = GradeRing->CreateDynamicMaterialInstance(0, RingMaterial))
			MID->SetVectorParameterValue(TEXT("Color"), JMUIStyle::GradeColor(this, Row ? static_cast<uint8>(Row->Grade) : 0));
	}
}

void ADropItemActor::PlaceMeshOnGround()
{
	// 바운드 가운데를 서버 위치(액터 xy)로, 바운드 바닥을 지면(+1cm)으로. 부착용 메시는 원점이 손잡이·머리 쪽이라 그대로 두면 치우쳤다
	Mesh->SetRelativeLocation(FVector::ZeroVector);
	Mesh->UpdateComponentToWorld();
	Mesh->UpdateBounds();
	const FBoxSphereBounds B = Mesh->Bounds;
	const FVector ActorLoc = GetActorLocation();
	const FVector Offset(ActorLoc.X - B.Origin.X, ActorLoc.Y - B.Origin.Y, GroundZ + MeshLiftCm - (B.Origin.Z - B.BoxExtent.Z));
	Mesh->SetWorldLocation(Mesh->GetComponentLocation() + Offset);
}

void ADropItemActor::StartFall(float FromZ)
{
	if (FromZ <= GroundZ + 1.f) return;
	bFalling = true;
	FallVelocity = 0.f;
	const FVector Loc = GetActorLocation();
	SetActorLocation(FVector(Loc.X, Loc.Y, FromZ));
}

void ADropItemActor::FaceIconToCamera()
{
	if (!bIcon) return;
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (Camera == nullptr) return;
	const FVector ToCamera = Camera->GetCameraLocation() - Mesh->GetComponentLocation();
	// 엔진 평면은 XY에 누워 있다(법선 +Z). 롤 90으로 세우면 법선이 Y축이 되므로 yaw를 카메라 방향 − 90으로 둔다(양면 머티리얼)
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(ToCamera.Y, ToCamera.X)) - 90.f;
	Mesh->SetWorldRotation(FRotator(0.f, Yaw, 90.f));
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

	if (bFalling)
	{
		FallVelocity += FallGravity * DeltaTime;
		FVector Loc = GetActorLocation();
		Loc.Z -= FallVelocity * DeltaTime;
		if (Loc.Z <= GroundZ)
		{
			Loc.Z = GroundZ;
			bFalling = false;
		}
		SetActorLocation(Loc);
	}

	FaceIconToCamera();
}
