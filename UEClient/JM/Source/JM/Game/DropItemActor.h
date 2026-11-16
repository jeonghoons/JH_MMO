#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DropItemActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;

// 바닥 드랍의 표시 액터 (A6.2, D-157). 서버의 OBJECT_TYPE_ITEM 오브젝트 하나 = 이 액터 하나.
// 서버가 SC_ADD_OBJECT로 알려 준 것을 UJMObjectManager가 만들고 SC_REMOVE_OBJECT로 지운다 — 클라이언트는 표시만 한다.
// 표시는 공용 메시 하나이고 아이템·골드만 구분한다(메시·머티리얼은 BP_DropItem에서 지정). 충돌은 없다(줍기는 거리로 판정).
UCLASS()
class JM_API ADropItemActor : public AActor
{
	GENERATED_BODY()

public:
	ADropItemActor();

	// ObjectId = 서버 오브젝트 id, ItemId = ItemTable ID(0 = 골드), Count = 수량(골드는 금액)
	void InitDrop(int32 InObjectId, int32 InItemId, int64 InCount);

	int32 GetObjectId() const { return ObjectId; }
	int32 GetItemId() const { return ItemId; }
	int64 GetCount() const { return Count; }
	bool IsGold() const { return ItemId == 0; }

	// 줍기 안내에 쓰는 이름: "HP 물약 x2", "골드 22"
	FText GetDisplayText() const;

	virtual void Tick(float DeltaTime) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drop")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// 아이템 드랍과 골드 드랍의 모양. 비워 두면 보이지 않는다(줍기는 된다).
	UPROPERTY(EditAnywhere, Category = "Drop")
	TObjectPtr<UStaticMesh> ItemMesh;
	UPROPERTY(EditAnywhere, Category = "Drop")
	TObjectPtr<UStaticMesh> GoldMesh;
	UPROPERTY(EditAnywhere, Category = "Drop")
	TObjectPtr<UMaterialInterface> ItemMaterial;
	UPROPERTY(EditAnywhere, Category = "Drop")
	TObjectPtr<UMaterialInterface> GoldMaterial;
	UPROPERTY(EditAnywhere, Category = "Drop")
	FVector ItemScale = FVector(0.25f);
	UPROPERTY(EditAnywhere, Category = "Drop")
	FVector GoldScale = FVector(0.2f);

	// 바닥에서 띄우는 높이(cm)와 도는 속도(도/초)
	UPROPERTY(EditAnywhere, Category = "Drop")
	float HoverHeight = 35.f;
	UPROPERTY(EditAnywhere, Category = "Drop")
	float SpinDegPerSec = 90.f;

private:
	int32 ObjectId = 0;
	int32 ItemId = 0;
	int64 Count = 0;
};
