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
	// TargetId: 대상 지정 스킬의 대상(SC_ATTACK.target_id). 투사체 연출이 이 대상을 향해 날아간다.
	void HandleAttack(int32 AttackerId, int32 SkillId, int32 TargetId, float AimYaw, float AimX, float AimY, int32 CooldownMs, uint32 Seq);
	// SC_ATTACK_RESULT(CP1): 서버가 내 시전을 거부했다. Result = Protocol::AttackResult
	void HandleAttackResult(int32 Result, uint32 Seq, int32 RemainMs);
	void HandleDamage(int32 AttackerId, int32 TargetId, int32 Damage, int32 RemainHp);
	// ReviveDelayMs: 내 캐릭터가 죽었을 때 부활 버튼이 켜지기까지의 시간(SC_DEAD.revive_delay_ms, 서버 값)
	void HandleDead(int32 CharacterId, int32 ReviveDelayMs);
	// SC_REVIVE(A2.4): 사망 해제·순간이동·HP 반영. 내 캐릭터면 보정 순번·HUD·부활 창까지.
	void HandleRevive(int32 CharacterId, const Protocol::PositionInfo& Pos, int32 Hp, uint32 CorrectionSeq);
	// SC_TELEPORT(L5): 보간 없이 서버가 정한 자리로 옮긴다. 내 캐릭터면 보정 순번까지.
	void HandleTeleport(int32 CharacterId, const Protocol::PositionInfo& Pos, uint32 CorrectionSeq);
	// SC_ROOM_CHANGE: Room이 바뀌었다.
	// 같은 레벨(DG3b): 다른 Room의 주변 오브젝트를 지우고 내 캐릭터는 남겨 새 자리로 옮긴다.
	// 다른 레벨(DG3c-2, 던전 입장·퇴장): 내 캐릭터의 순번·쿨타임을 TravelCarry에 담고 UNetworkManager::TravelToRoomLevel로 레벨을 연다.
	// 새 레벨에서 AJMPlayerController::BeginPlay가 CS_ENTER_ROOM("로딩 끝")을 보내면 본인 SC_ADD_OBJECT가 오고 HandleSpawn이 담아 둔 값을 넣는다.
	void HandleRoomChange(const Protocol::SC_ROOM_CHANGE_PACKET& Pkt);
	// SC_DUNGEON_STATE(DG3c): 던전에 들어온 직후 한 번, 우두머리를 잡으면 한 번 온다. 나갈 때는 오지 않으므로 Room이 바뀔 때 지운다.
	void HandleDungeonState(const Protocol::SC_DUNGEON_STATE_PACKET& Pkt);
	// 지금 던전 상태(Protocol::DungeonState)와 클리어 뒤 내보내는 시각(FPlatformTime::Seconds, 클리어가 아니면 0). HUD가 만들어질 때 읽는다.
	int32 GetDungeonState() const { return DungeonState; }
	double GetDungeonClearEndSec() const { return DungeonClearEndSec; }

	// 레벨이 바뀌는 Room 이동이 진행 중인가(SC_ROOM_CHANGE를 받은 뒤 새 레벨에서 내 캐릭터가 스폰되기 전까지)
	bool IsRoomTravelActive() const { return TravelCarry.bActive; }
	// CS_ENTER_ROOM을 보낸 시각을 적는다(로딩 시간 로그용). UNetworkManager::SendEnterRoom이 부른다
	void NoteEnterRoomSent();
	// 로그인 화면으로 돌아가거나 새로 로그인할 때: 넘기려던 값·사망 시각을 버린다(UNetworkManager::SwitchGameMapLevel)
	void ResetRoomTravel();
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
	// 성장·MP(A2.3b, D-254). 값만 반영하고 로그를 남긴다(경험치·MP HUD는 다음 항목).
	// SC_GAIN_EXP: 내 캐릭터의 경험치(지금 레벨 안의 누적). SC_CHANGE_MP: 내 캐릭터의 MP.
	void HandleGainExp(int64 Exp, int64 Gained);
	void HandleChangeMp(int32 Mp);
	// SC_LEVEL_UP(AOI, 본인 포함): 그 캐릭터의 레벨·스탯(HP·MP 가득). 내 캐릭터면 HUD HP까지.
	void HandleLevelUp(int32 ObjectId, int32 Level, const Protocol::StatInfo& Stat);

	// From에서 수평 거리 RangeCm 안의 가장 가까운 바닥 드랍(A6.2). 없으면 nullptr. 서버도 수평 거리로 판정한다.
	ADropItemActor* FindNearestDrop(const FVector& From, float RangeCm) const;

	// 마을 NPC(L4). From에서 RangeCm 안(수평 거리 — 서버와 같은 기준)의 가장 가까운 NPC. 없으면 0
	// KindFilter = 찾을 NPC의 일(Protocol::NpcKind — 상점·경매, A4.3·D-234). 0이면 가리지 않는다
	int32 FindNearestNpc(const FVector& From, float RangeCm, float& OutDistSq, int32 KindFilter = 0) const;
	// 그 NPC가 하는 일(Protocol::NpcKind). 없는 NPC면 0
	int32 GetNpcKind(int32 NpcId) const;
	FString GetNpcName(int32 NpcId) const;
	bool GetNpcLocation(int32 NpcId, FVector& OutLocation) const;
	// NPC 종류 ID(NpcTable ID = ObjectInfo.template_id). 없으면 0 (Q1 — QuestTable.NpcId와 맞춘다)
	int32 GetNpcTemplateId(int32 NpcId) const;
	// 시야 안에서 그 종류 ID의 NPC 이름. 없으면 빈 글
	FString FindNpcNameByTemplate(int32 TemplateId) const;

	// 포털(L5). From이 반경 안(수평 거리 — 서버와 같은 기준)에 든 포털 중 가장 가까운 것의 id. 없으면 0
	// OutKind = 그 포털의 종류(Protocol::PortalKind — 일반·던전 입구·던전 출구, 안내 문구용)
	int32 FindNearestPortal(const FVector& From, float& OutDistSq, int32* OutKind = nullptr) const;

	// SkillTable의 Fx를 HitShape에 맞춰 재생한다. 장판은 조준 지점에 DurationMs 동안, 부채꼴·직선은 타격 시점에 시전자 발밑에서 1회,
	// 투사체는 타격 시점(HitDelayMs = 시위를 놓는 순간)에 화살 연출(AProjectileFx)을 TargetId 쪽으로 ProjSpeed로 날린다(D-254 — 판정은 서버).
	// OutComp·OutTimer(CP1): 내 캐릭터가 먼저 시작한 이펙트를 거부 때 지울 수 있게 장판 컴포넌트와 타이머(장판 끄기 또는 타격 시점 대기)를 돌려준다.
	void SpawnSkillFx(AJMCharacterBase* Attacker, const struct FSkillTableRow& Skill, int32 TargetId, float AimYaw, float AimX, float AimY,
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
		int32 Kind = 0;   // Protocol::PortalKind
	};
	TArray<FPortalEntry> Portals;

private:
	void ClearDungeonState();

	// 피격 이펙트(D-254): SC_DAMAGE에는 스킬 ID가 없어 공격자별 마지막 SC_ATTACK 스킬을 적어 둔다(공격자 id → 스킬 id).
	// 몬스터 스킬(9xxx)은 SkillTable에 없을 수 있어 그때는 재생하지 않는다. Room이 바뀌면 비운다.
	TMap<int32, int32> LastAttackSkill;
	void SpawnHitFx(int32 AttackerId, AJMCharacterBase* Target);

	// 레벨이 바뀌는 Room 이동에서 새 레벨의 내 캐릭터에 넘길 값(DG3c-2). 이 매니저는 GameInstance 수명이라 레벨 전환에도 남는다.
	// 서버는 Room 이동 때 보정 순번을 +1만 하고 쿨타임도 이어 가는데, SC_ADD_OBJECT에는 그 값이 없어 SC_ROOM_CHANGE에서 받아 둬야 한다.
	struct FRoomTravelCarry
	{
		bool bActive = false;
		FString MapName;                      // 가는 중인 레벨(짧은 이름). 도착 전의 "지금 레벨" 비교에 쓴다
		uint32 CorrectionSeq = 0;             // SC_ROOM_CHANGE.correction_seq
		uint32 AttackSeq = 0;
		uint32 AimSeq = 0;
		TMap<int32, double> SkillCooldownEnd; // 스킬 ID → 끝 시각(FPlatformTime::Seconds)
		double RecvSec = 0.0;                 // SC_ROOM_CHANGE를 받은 시각
		double EnterSentSec = 0.0;            // CS_ENTER_ROOM을 보낸 시각
	};
	FRoomTravelCarry TravelCarry;

	// 내 캐릭터가 부활할 수 있게 되는 시각(FPlatformTime::Seconds, 0 = 죽지 않음). 죽은 채 다른 레벨에 도착하면(파티장 따라가기·내보냄)
	// 서버가 SC_DEAD를 다시 보내지 않으므로, 이 값으로 부활 창의 남은 시간을 다시 맞춘다.
	double MyReviveReadySec = 0.0;

	int32 DungeonState = 0;            // Protocol::DungeonState
	double DungeonClearEndSec = 0.0;
};
