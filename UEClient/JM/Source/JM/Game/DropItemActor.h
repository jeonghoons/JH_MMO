#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DropItemActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;

// 바닥 드랍의 표시 액터 (A6.2, D-157). 서버의 OBJECT_TYPE_ITEM 오브젝트 하나 = 이 액터 하나.
// 서버가 SC_ADD_OBJECT로 알려 준 것을 UJMObjectManager가 만들고 SC_REMOVE_OBJECT로 지운다 — 클라이언트는 표시만 한다.
// 외형(FP1, D-287): 아이템 자체 메시(ItemTable.StaticMesh — 투구·무기·방패)를 바닥에 눕혀 놓고, 없으면(상의·하의·장갑·신발·소비·전리품)
// 아이콘 판(ItemTable.Icon)을 카메라 쪽으로 세운다. 밑에는 등급 색 원판, 골드는 원판 모양. 충돌은 없다(줍기는 거리로 판정).
UCLASS()
class JM_API ADropItemActor : public AActor
{
	GENERATED_BODY()

public:
	ADropItemActor();

	// ObjectId = 서버 오브젝트 id, ItemId = ItemTable ID(0 = 골드), Count = 수량(골드는 금액)
	void InitDrop(int32 InObjectId, int32 InItemId, int64 InCount);
	// 방금 생긴 드랍: 이 높이(월드 z — 몬스터 시체·플레이어의 몸 가운데)에서 바닥까지 떨어진다(회전 없음)
	void StartFall(float FromZ);

	int32 GetObjectId() const { return ObjectId; }
	int32 GetItemId() const { return ItemId; }
	int64 GetCount() const { return Count; }
	bool IsGold() const { return ItemId == 0; }

	// 줍기 안내에 쓰는 이름: "HP 물약 x2", "골드 22"
	FText GetDisplayText() const;

	virtual void Tick(float DeltaTime) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drop")
	TObjectPtr<USceneComponent> Root;
	// 아이템 메시·아이콘 판·골드 원판
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drop")
	TObjectPtr<UStaticMeshComponent> Mesh;
	// 등급 색 바닥 원판(골드는 숨김)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drop")
	TObjectPtr<UStaticMeshComponent> GradeRing;

	// 골드 모양(BP_DropItem에서 지정). 비워 두면 보이지 않는다(줍기는 된다).
	UPROPERTY(EditAnywhere, Category = "Drop")
	TObjectPtr<UStaticMesh> GoldMesh;
	UPROPERTY(EditAnywhere, Category = "Drop")
	TObjectPtr<UMaterialInterface> GoldMaterial;
	UPROPERTY(EditAnywhere, Category = "Drop")
	FVector GoldScale = FVector(0.2f);

	// 아이콘 판(평면 메시 + 텍스처 파라미터 "Icon"인 머티리얼)과 등급 원판(원기둥 + 벡터 파라미터 "Color")
	UPROPERTY(EditAnywhere, Category = "Drop")
	TObjectPtr<UStaticMesh> IconPlaneMesh;
	UPROPERTY(EditAnywhere, Category = "Drop")
	TObjectPtr<UMaterialInterface> IconMaterial;
	UPROPERTY(EditAnywhere, Category = "Drop")
	TObjectPtr<UStaticMesh> RingMesh;
	UPROPERTY(EditAnywhere, Category = "Drop")
	TObjectPtr<UMaterialInterface> RingMaterial;

	// 크기(cm): 자체 메시의 가장 긴 변(투구는 HelmMaxLenCm), 아이콘 판 한 변, 아이콘 판 가운데 높이, 등급 원판 지름
	UPROPERTY(EditAnywhere, Category = "Drop")
	float MeshMaxLenCm = 70.f;
	UPROPERTY(EditAnywhere, Category = "Drop")
	float HelmMaxLenCm = 35.f;
	UPROPERTY(EditAnywhere, Category = "Drop")
	float IconSizeCm = 45.f;
	UPROPERTY(EditAnywhere, Category = "Drop")
	float IconHeightCm = 30.f;
	UPROPERTY(EditAnywhere, Category = "Drop")
	float RingDiameterCm = 55.f;
	// 떨어질 때 중력(cm/s²)
	UPROPERTY(EditAnywhere, Category = "Drop")
	float FallGravity = 980.f;

private:
	// 바닥 높이(고정 지형·충돌 도형만 — 시체·플레이어 캡슐은 보지 않는다). 못 찾으면 서버 z
	float TraceGroundZ(const FVector& At) const;
	// 메시를 바닥 기준으로 놓는다: 바운드 바닥이 땅에 닿고 가운데가 서버 위치에 오게
	void PlaceMeshOnGround();
	void FaceIconToCamera();

	int32 ObjectId = 0;
	int32 ItemId = 0;
	int64 Count = 0;
	bool bIcon = false;
	float GroundZ = 0.f;
	bool bFalling = false;
	float FallVelocity = 0.f;
};
