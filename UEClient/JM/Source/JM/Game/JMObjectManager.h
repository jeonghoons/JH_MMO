#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Protocol/Protocol.pb.h"
#include "JMObjectManager.generated.h"

class AJMCharacterBase;
class AJMMyPlayer;
class ADropItemActor;

UCLASS()
class JM_API UJMObjectManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	void ClearObjects();

	void HandleSpawn(const Protocol::ObjectInfo& ObjInfo);
	void HandleDespawn(int32 ObjectId);
	// 내 캐릭터에 대한 SC_MOVE는 서버가 내 이동을 거부했다는 보정이다(CorrectionSeq와 함께 적용).
	void HandleMove(const Protocol::ObjectInfo& ObjInfo, uint32 CorrectionSeq);
	// CooldownMs: 서버가 적용한 쿨타임(SC_ATTACK.cooldown_ms). 내 시전이면 HUD 슬롯에 표시한다.
	// Seq: 시전자가 보낸 요청 순번(SC_ATTACK.seq). 내 시전은 입력 때 이미 시작했으므로(CP1) 순번이 있으면 다시 재생하지 않는다.
	void HandleAttack(int32 AttackerId, int32 SkillId, float AimYaw, float AimX, float AimY, int32 CooldownMs, uint32 Seq);
	// SC_ATTACK_RESULT(CP1): 서버가 내 시전을 거부했다. Result = Protocol::AttackResult
	void HandleAttackResult(int32 Result, uint32 Seq, int32 RemainMs);
	void HandleDamage(int32 AttackerId, int32 TargetId, int32 Damage, int32 RemainHp);
	// ReviveDelayMs: 내 캐릭터가 죽었을 때 부활 버튼이 켜지기까지의 시간(SC_DEAD.revive_delay_ms, 서버 값)
	void HandleDead(int32 CharacterId, int32 ReviveDelayMs);
	// SC_REVIVE(A2.4): 사망 해제·순간이동·HP 반영. 내 캐릭터면 보정 순번·HUD·부활 창까지.
	void HandleRevive(int32 CharacterId, const Protocol::PositionInfo& Pos, int32 Hp, uint32 CorrectionSeq);
	// SC_TELEPORT(L5): 보간 없이 서버가 정한 자리로 옮긴다. 내 캐릭터면 보정 순번까지.
	void HandleTeleport(int32 CharacterId, const Protocol::PositionInfo& Pos, uint32 CorrectionSeq);
	// SC_PORTAL_LIST(L5): 방 입장 직후 서버가 알려 주는, 지금 쓸 수 있는 포털의 위치·반경
	void HandlePortalList(const Protocol::SC_PORTAL_LIST_PACKET& Pkt);
	// SC_CHANGE_EQUIP: 장착 목록·스탯 반영, 내 캐릭터면 HUD(HP·무기 타입별 스킬 슬롯) 갱신
	void HandleChangeEquip(int32 ObjectId, Protocol::EquipPart Part, int32 ItemId, const Protocol::StatInfo& Stat);
	// SC_SKILL_AIM(MC4.2d-1): 원격은 그대로 반영, 내 캐릭터는 해제(거부·시간 초과)만 Seq가 맞을 때 반영한다.
	void HandleSkillAim(int32 ObjectId, int32 SkillId, bool bActive, uint32 Seq);
	// SC_USE_ITEM(A3.6): 쓴 캐릭터의 HP 반영. 내 캐릭터면 HUD HP와 그 종류가 든 액티브 칸의 재사용 표시까지.
	void HandleUseItem(int32 ObjectId, int32 ItemId, int32 Hp, int32 CooldownMs);
	// SC_CHANGE_HP(L3): 피격 연출 없이 HP만 반영(몬스터가 홈으로 돌아가 회복). 내 캐릭터면 HUD HP까지.
	void HandleChangeHp(int32 ObjectId, int32 Hp);

	// From에서 수평 거리 RangeCm 안의 가장 가까운 바닥 드랍(A6.2). 없으면 nullptr. 서버도 수평 거리로 판정한다.
	ADropItemActor* FindNearestDrop(const FVector& From, float RangeCm) const;

	// 마을 NPC(L4). From에서 RangeCm 안(수평 거리 — 서버와 같은 기준)의 가장 가까운 NPC. 없으면 0
	int32 FindNearestNpc(const FVector& From, float RangeCm, float& OutDistSq) const;
	FString GetNpcName(int32 NpcId) const;
	bool GetNpcLocation(int32 NpcId, FVector& OutLocation) const;

	// 포털(L5). From이 반경 안(수평 거리 — 서버와 같은 기준)에 든 포털 중 가장 가까운 것의 id. 없으면 0
	int32 FindNearestPortal(const FVector& From, float& OutDistSq) const;

	// SkillTable의 Fx를 HitShape에 맞춰 재생한다. 장판은 조준 지점에 DurationMs 동안, 부채꼴·직선은 타격 시점에 시전자 발밑에서 1회.
	// OutComp·OutTimer(CP1): 내 캐릭터가 먼저 시작한 이펙트를 거부 때 지울 수 있게 장판 컴포넌트와 타이머(장판 끄기 또는 타격 시점 대기)를 돌려준다.
	void SpawnSkillFx(AJMCharacterBase* Attacker, const struct FSkillTableRow& Skill, float AimYaw, float AimX, float AimY,
		TWeakObjectPtr<class UParticleSystemComponent>* OutComp = nullptr, FTimerHandle* OutTimer = nullptr);

private:
	// 전투 HUD(UIManager에 HUD 이름으로 등록된 위젯). 로그인 화면처럼 HUD가 없으면 nullptr
	class UCombatHudWidget* GetCombatHud() const;


public:
	UPROPERTY()
	TObjectPtr<AJMMyPlayer> MyPlayer;
	UPROPERTY()
	TMap<int32, TObjectPtr<AJMCharacterBase>> Objects;
	// 바닥 드랍(OBJECT_TYPE_ITEM). Objects는 캐릭터 전용이라 따로 둔다 (A6.2, D-157)
	UPROPERTY()
	TMap<int32, TObjectPtr<ADropItemActor>> Drops;
	// 마을 NPC(OBJECT_TYPE_NPC, L4·D-202). 서버가 준 체형·장비 외형을 플레이어 액터 클래스로 그린다(매니퀸 + 기존 장비, D-170).
	// Objects(전투·거래 대상 탐색에 쓰는 캐릭터 목록)에는 넣지 않는다 — NPC는 공격·거래 대상이 아니다.
	UPROPERTY()
	TMap<int32, TObjectPtr<AJMCharacterBase>> Npcs;

	// 서버가 알려 준 포털(SC_PORTAL_LIST, L5). 클라이언트는 위치·반경만 알고 도착지는 서버가 정한다.
	struct FPortalEntry
	{
		int32 Id = 0;
		FVector Location = FVector::ZeroVector;
		float RadiusCm = 0.f;
	};
	TArray<FPortalEntry> Portals;
};
