#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "ItemTableRow.generated.h"

// Common/DataTable/SkillTable.csv·ItemTable.csv 의 enum 값과 이름이 같아야 한다. (서버 ServerData.h 의 WeaponType / ItemSlot / ItemTier / ItemGrade)

// 스킬이 종속되는 무기 타입. 플레이어는 장착 무기가 정하고(비면 Unarmed), None은 무기가 아닌 아이템에만 쓴다.
UENUM(BlueprintType)
enum class EWeaponType : uint8
{
	None,
	Unarmed,
	Sword,
	Bow,
	Monster
};

// 장비 슬롯. 번호 = itemId 첫째 자리 = Protocol::EquipPart (MC3.2)
UENUM(BlueprintType)
enum class EEquipSlot : uint8
{
	None = 0 UMETA(Hidden),
	Head = 1,
	Top = 2,
	Bottom = 3,
	Gloves = 4,
	Boots = 5,
	Weapon = 6
};

// 장비 티어(외형 계열). 번호 = itemId 둘째 자리.
UENUM(BlueprintType)
enum class EItemTier : uint8
{
	None = 0 UMETA(Hidden),
	Cloth = 1,
	Leather = 2,
	Plate = 3
};

// 등급. 데이터·아이콘 테두리에만 쓰고 외형은 티어가 정한다.
UENUM(BlueprintType)
enum class EItemGrade : uint8
{
	Common,
	Uncommon,
	Rare
};

// Common/DataTable/ItemTable.csv 의 한 행. 컬럼명은 CSV 헤더와 1:1이다.
// RowName = itemId 문자열(슬롯 1자리 + 티어 1자리 + 일련번호 2자리, 예: 6101 = 천 한손검).
// 스탯 수치의 권위는 서버이며, 클라이언트는 외형·UI에만 쓴다. 부착 소켓·상대 변환·몸 숨김 열은 MC4.1에서 추가한다.
USTRUCT(BlueprintType)
struct FItemTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) EEquipSlot Slot = EEquipSlot::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EItemGrade Grade = EItemGrade::Common;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EItemTier Tier = EItemTier::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EWeaponType WeaponType = EWeaponType::None;   // 무기만 Sword/Bow
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 BonusAttackDamage = 0;                  // 무기만 (표시용, 계산은 서버)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 BonusMaxHp = 0;                         // 방어구만 (표시용, 계산은 서버)

	// 이하 클라이언트 전용(서버 로더는 읽지 않음)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UTexture2D> Icon;
	// 스켈레탈 슬롯(상의·하의·장갑·장화)의 체형별 메시. Leader Pose로 몸을 따라간다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> MeshM;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> MeshF;
	// 정적 슬롯(머리·무기)의 메시. 본/소켓에 붙인다(체형 공용).
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UStaticMesh> StaticMesh;
};
