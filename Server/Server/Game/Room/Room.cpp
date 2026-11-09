#include "pch.h"
#include "Game/Room/Room.h"
#include "Network/Session.h"
#include "Packet/PacketHandler.h"
#include "Game/Object/Player.h"
#include "Game/Object/Monster.h"
#include "Game/Combat/CombatProcessor.h"
#include "Data/ServerData.h"
#include "Utils/Metrics.h"
#include "DB/DatabaseWorker.h"

Room::Room(shared_ptr<Timer> timer, HANDLE iocpHandle) : _timer(timer), _jobQueue(make_shared<JobQueue>(iocpHandle))
{
}
Room::~Room() = default;

void Room::InitRoom()
{
	if (!_gameMap.LoadMapData(ROOM_MAP_NAME))
	{
		LOG_ERROR("Room", "맵 초기화(LoadMapData) 중 일부 문제가 발생했습니다.");
	}
	_gameMap.Init(weak_from_this());

	const auto& monsterSpawns = _gameMap.GetMonsterSpawns();
	if (monsterSpawns.empty())
	{
		LOG_ERROR("Room", "스폰 포인트가 없어 몬스터를 생성할 수 없습니다!");
	}
	else
	{
		for (int i = 0; i < static_cast<int>(monsterSpawns.size()); ++i) {
			for (int n = 0; n < max(1, monsterSpawns[i].MaxCount); ++n)
				SpawnMonster(i);
		}
	}
	
	long long currentTick = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	Update(currentTick);
}

// [Debug] DummySpawnZone 원 안에서 내비메시 위의 점을 고른다(핫존·시야 경계 시험). 구역이 없거나 다 찼거나 못 찾으면 false
bool Room::PickDummyZonePosition(Protocol::PositionInfo& out)
{
	// 서버 전체에서 들어온 순서대로 앞 구역부터 n명씩 채운다(방이 여러 개여도 순서는 하나).
	static atomic<int> dummyIndex = 0;
	int index = dummyIndex.fetch_add(1);
	const ServerConfig::DummyZone* zone = nullptr;
	for (const ServerConfig::DummyZone& z : GServerConfig.dummyZones)
	{
		if (z.count == 0 || index < z.count) { zone = &z; break; }
		index -= z.count;
	}
	if (zone == nullptr) return false;

	const float cx = zone->x, cy = zone->y, r = zone->r;
	const float zGuess = _gameMap.GetPlayerStarts().empty() ? 0.f : _gameMap.GetPlayerStarts()[0].Pos.Z;
	for (int i = 0; i < 30; ++i)
	{
		const float angle = Utils::GetRandom<float>(0.f, 6.2831853f);
		const float dist = r * sqrtf(Utils::GetRandom<float>(0.f, 1.f));
		out.set_x(cx + cosf(angle) * dist);
		out.set_y(cy + sinf(angle) * dist);
		out.set_z(zGuess);
		if (!_gameMap.GetNavManager()->AdjustPositionToNavMesh(out)) continue;
		const float dx = out.x() - cx, dy = out.y() - cy;
		if (dx * dx + dy * dy <= r * r) return true;
	}
	LOG_WARN("Room", "DummySpawnZone({},{},{}) 안에서 내비메시 위치를 찾지 못해 무작위 위치에 배치합니다", cx, cy, r);
	return false;
}

// [Debug] ViewAudit: 플레이어마다 방 안의 모든 캐릭터와의 실제 수평 거리를 직접 계산해(전수 검사 = 정답) 시야 목록과 비교한다.
// 거리 <= R 인데 시야에 없으면 누락, 거리 > R+H 인데 시야에 있으면 초과. R < 거리 <= R+H 는 어느 쪽이어도 정상(히스테리시스).
void Room::AuditViews()
{
	static atomic<int> warned = 0;   // 처음 몇 건만 로그로 남긴다
	const float r = _gameMap.GetViewRadius();
	const float rOut = r + _gameMap.GetViewHysteresis();

	long long must = 0, missing = 0, mustNot = 0, extra = 0;
	for (const auto& [viewerId, viewer] : _players)
	{
		const Protocol::PositionInfo& vp = viewer->GetPosition();
		const vector<int>& viewList = viewer->_viewList;
		for (const auto& [targetId, target] : _objects)
		{
			if (targetId == viewerId) continue;
			const Protocol::PositionInfo& tp = target->GetPosition();
			const float dist = sqrtf((tp.x() - vp.x()) * (tp.x() - vp.x()) + (tp.y() - vp.y()) * (tp.y() - vp.y()));
			const bool inView = find(viewList.begin(), viewList.end(), targetId) != viewList.end();

			const char* kind = nullptr;
			if (dist <= r) { ++must; if (!inView) { ++missing; kind = "누락"; } }
			else if (dist > rOut) { ++mustNot; if (inView) { ++extra; kind = "초과"; } }

			if (kind && warned.fetch_add(1) < 10)
				LOG_WARN("ViewAudit", "{}: 플레이어 {} → 대상 {} 거리 {:.0f}cm (R {:.0f}, R+H {:.0f})", kind, viewerId, targetId, dist, r, rOut);
		}
	}
	Metrics::ViewAuditMust.fetch_add(must);
	Metrics::ViewAuditMissing.fetch_add(missing);
	Metrics::ViewAuditMustNot.fetch_add(mustNot);
	Metrics::ViewAuditExtra.fetch_add(extra);
}

void Room::Update(long long elapsedTime)
{
	long long currentTick = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	ReserveJob(100, &Room::Update, currentTick);

	float dt = (currentTick - elapsedTime) / 1000.f;
	// 예정 간격(100ms)보다 얼마나 늦게 틱이 돌았는지 기록한다. 첫 틱(InitRoom에서 직접 호출)은 제외한다.
	if (elapsedTime != currentTick)
		Metrics::TickLate.Add(max<long long>(0, (currentTick - elapsedTime - 100) * 1000));

	// 서버가 멈췄다 재개되는 등 틱 간격이 비정상이면 한 번에 큰 이동이 일어나지 않게 기본 간격으로 대체한다.
	if (dt <= 0.f || dt > 0.5f) dt = 0.1f;

	const auto bodyStart = chrono::steady_clock::now();
	for (const auto& object : _objects) {
		object.second->Update(dt);
	}
	UpdateZones();
	UpdateAims(currentTick);
	Metrics::TickBody.Add(chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - bodyStart).count());

	// 1초(10틱)마다 플레이어별 시야 인원을 표본으로 남긴다(지표용, 틱 본문 시간에는 넣지 않음).
	if (++_tickCount % 10 == 0)
	{
		if (GServerConfig.viewAudit) AuditViews();

		const int nodes = _gameMap.GetAoi().NodeCount(), depth = _gameMap.GetAoi().MaxDepth();
		int prev = Metrics::AoiNodes.load();
		while (nodes > prev && !Metrics::AoiNodes.compare_exchange_weak(prev, nodes)) {}
		prev = Metrics::AoiDepth.load();
		while (depth > prev && !Metrics::AoiDepth.compare_exchange_weak(prev, depth)) {}

		for (const auto& [id, player] : _players)
		{
			long long viewPlayers = 0;
			for (int viewId : player->_viewList)
				if (_players.count(viewId)) ++viewPlayers;
			Metrics::ViewPlayers.Add(viewPlayers);
			Metrics::ViewObjects.Add(static_cast<long long>(player->_viewList.size()));
		}
	}
}

bool Room::AddObject(shared_ptr<GameObject> object)
{
	int objectId = object->GetId();

	auto [it, success] = _objects.emplace(objectId, object);
	if (!success) {
		LOG_ERROR("Room", "AddObject: 이미 있는 오브젝트 id {}", objectId);
		return false;
	}
	object->SetOwnerRoom(shared_from_this());

	const auto& playerStarts = _gameMap.GetPlayerStarts();
	if (object->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER && !playerStarts.empty()) {
		Protocol::PositionInfo spawnPos;
		spawnPos.set_x(playerStarts[0].Pos.X);
		spawnPos.set_y(playerStarts[0].Pos.Y);
		spawnPos.set_z(playerStarts[0].Pos.Z);
		object->SetPosition(spawnPos);
				
		shared_ptr<Player> dummyPlayer = static_pointer_cast<Player>(object);
		if (dummyPlayer->isDummy) {
			Protocol::PositionInfo dummyPos;
			if (!PickDummyZonePosition(dummyPos))
				dummyPos = _gameMap.GetNavManager()->GetRandomPosition();
			dummyPlayer->SetPosition(dummyPos);
		}
	}

	Protocol::PositionInfo currentPos = object->GetPosition();

	if (_gameMap.IsOutOfBounds(currentPos))
	{
		LOG_WARN("Room", "오브젝트 스폰 위치가 맵(NavMesh) 바깥이거나 바닥이 없습니다! ID: {}, {}, {}, {}",
			objectId, currentPos.x(), currentPos.y(), currentPos.z());
	}

	object->SetPosition(currentPos);

	return true;
}

bool Room::RemoveObject(int objectId)
{
	return _objects.erase(objectId) > 0;
}

void Room::PlayerEnterRoom(shared_ptr<Player> player)
{
	if (false == AddObject(player)) {
		ReleaseSlot(); // EnterPlayer에서 예약한 자리를 반환한다.
		return;
	}

	_players.emplace(player->GetId(), player);

	// 이동 검증 상태: 예산 0에서 시작해 시간이 지나면 모인다.
	player->moveGuard = {};
	player->moveGuard.lastAccrueMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	// 조준 상태도 새로 시작한다(seq는 클라이언트 값이라 그대로 둔다).
	player->SetSkillAim(nullptr, player->moveGuard.lastAccrueMs);
	
	LOG_DEBUG("Room", "Client[{}] Enter Room", player->GetId());

	shared_ptr<SendBuffer> objectAddBuffer = PacketSerializer::MAKE_SC_ADD_OBJECT(player);
	if (auto session = player->GetSession()) {
		session->Send(objectAddBuffer);
	}

	if (auto session = player->GetSession()) {
		session->Send(PacketSerializer::MAKE_SC_ADD_OBJECT(player));
	}

	// 맵 등록 및 시야 동기화
	ViewUpdate result = _gameMap.EnterMap(player->GetId(), player->GetPosition());
	if (!result.entered.empty()) {
		UpdateView(player, result);
	}
}

void Room::PlayerLeaveRoom(shared_ptr<Player> player)
{
	LOG_DEBUG("Room", "Player[{}] Leave", player->GetId());

	int playerId = player->GetId();

	ViewUpdate result = _gameMap.LeaveMap(playerId, player->_viewList);
	if (!result.leaved.empty()) {
		UpdateView(player, result);
	}


	if (false == RemoveObject(playerId)) {
		LOG_ERROR("Room", "LeaveRoom: 방에 없는 플레이어 id {}", playerId);
	}

	if (_players.erase(playerId) > 0)
		ReleaseSlot();

	// 조준 중이었다면 상태만 지운다(SC_REMOVE_OBJECT로 다른 사람의 표시는 사라진다).
	if (player->IsSkillAiming())
		player->SetSkillAim(nullptr, chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count());

	// 저장에 실패해 남은 변경이 있으면 나갈 때 다시 저장한다. (진행 중인 저장은 OnEquipSaved에서 마무리된다)
	if (player->equipSaveDirty && !player->equipSaveInFlight)
		RequestEquipSave(player);

}

void Room::PlayerMove(shared_ptr<Player> player, Protocol::PositionInfo position, uint32_t correctionSeq)
{
	if (nullptr == Id2Player(player->GetId()))
		return;

	Player::MoveGuard& guard = player->moveGuard;

	// 1) 마지막 보정을 받기 전에 보낸 이동이다. 이미 보정 위치로 되돌렸으니 조용히 버린다(보정이 연쇄되지 않게).
	if (correctionSeq != guard.correctionSeq)
	{
		Metrics::MoveDroppedStale.fetch_add(1);
		return;
	}

	const MovementData* rule = DataManager::GetMovementData(PLAYER_MOVEMENT_ID);   // 서버 시작 때 존재를 확인했다
	const long long nowMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();

	// 2) 너무 잦은 이동 패킷은 처리하지 않는다(내비메시 검사 비용 보호). 다음 패킷이 그만큼의 거리를 함께 가져온다.
	if (nowMs - guard.lastRecvMs < rule->minMoveIntervalMs)
	{
		Metrics::MoveDroppedRate.fetch_add(1);
		return;
	}
	guard.lastRecvMs = nowMs;

	const Protocol::PositionInfo currentPos = player->GetPosition();

	// 이동을 거부하고 본인에게 서버 위치를 보낸다. 클라이언트는 그 위치로 돌아가고 이후 이동에 새 순번을 싣는다.
	auto reject = [&]() {
		++guard.correctionSeq;
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_MOVE_OBJECT(player, guard.correctionSeq));
	};

	// 3) 숫자가 아닌 값
	if (!std::isfinite(position.x()) || !std::isfinite(position.y()) || !std::isfinite(position.z()) || !std::isfinite(position.yaw()) ||
		!std::isfinite(position.v_x()) || !std::isfinite(position.v_y()) || !std::isfinite(position.v_z()))
	{
		Metrics::MoveRejectInvalid.fetch_add(1);
		reject();
		return;
	}

	// 4) 속도: 경과 시간 × 이동 속도 × 허용 배율만큼 예산이 모이고(상한 = BudgetMs 분량), 이동 거리만큼 쓴다.
	//    지연으로 패킷이 몰려 와도 평균 속도가 맞으면 통과한다.
	//    스킬 조준 중이면 속도 상한에 AimMoveSpeedMul이 곱해진다(MC4.2d-1).
	player->AccrueMoveBudget(nowMs, *rule);
	const float maxSpeed = player->GetMoveSpeedLimit(*rule);

	const float dx = position.x() - currentPos.x(), dy = position.y() - currentPos.y();
	const float dist = sqrtf(dx * dx + dy * dy);
	if (dist > guard.budgetCm + rule->moveSlackCm)
	{
		Metrics::MoveRejectSpeed.fetch_add(1);
		reject();
		return;
	}

	// 5) 지형: 현재 위치에서 목표까지 내비메시 위로 막힘 없이 갈 수 있어야 한다(경계 밖 ServerNavToleranceCm까지 허용).
	if (dist > 0.f && !_gameMap.CanMove(currentPos, position, rule->navToleranceCm))
	{
		Metrics::MoveRejectNav.fetch_add(1);
		reject();
		return;
	}

	guard.budgetCm = max(0.f, guard.budgetCm - dist);
	Metrics::MoveAccepted.fetch_add(1);

	// 다른 클라이언트의 외삽에 쓰이는 속도는 허용 속도로 자른다.
	const float speed2D = sqrtf(position.v_x() * position.v_x() + position.v_y() * position.v_y());
	if (speed2D > maxSpeed)
	{
		const float s = maxSpeed / speed2D;
		position.set_v_x(position.v_x() * s);
		position.set_v_y(position.v_y() * s);
	}

	player->SetPosition(position);

	shared_ptr<SendBuffer> playerMoveBuffer = PacketSerializer::MAKE_SC_MOVE_OBJECT(player);
	BroadcastAOI(static_pointer_cast<Character>(player), playerMoveBuffer);

	ViewUpdate result = _gameMap.UpdateMap(player->GetId(), player->GetPosition(), player->_viewList);

	if (!result.entered.empty() || !result.leaved.empty()) {
		UpdateView(player, result);
	}

}

void Room::PlayerPing(shared_ptr<Player> player, long long clientTime)
{
	if (shared_ptr<Session> session = player->GetSession())
		session->Send(PacketSerializer::MAKE_SC_PONG(clientTime));
}

void Room::PlayerSkillAim(shared_ptr<Player> player, int slot, bool active, uint32_t seq)
{
	if (player == nullptr || Id2Player(player->GetId()) == nullptr) return;

	const long long nowMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	player->aim.seq = seq;

	// 끄기: 조준 중일 때만 해제하고 다른 사람에게 알린다. 빈도 제한 없이 항상 받는다(버리면 서버만 느린 상태로 남는다).
	if (!active)
	{
		if (!player->IsSkillAiming()) return;
		player->SetSkillAim(nullptr, nowMs);
		Metrics::AimOff.fetch_add(1);
		BroadcastAOI(static_pointer_cast<Character>(player), PacketSerializer::MAKE_SC_SKILL_AIM(player->GetId(), 0, false, 0));
		return;
	}

	// 켜기: 생존 → 조준할 수 있는 스킬(슬롯 1~5, AimMaxMs > 0) → 쿨타임 → 빈도. 피격 경직·시전 중은 허용한다(확정할 때 CastSkill이 다시 본다).
	const SkillData* skill = player->FindSlotSkill(slot);
	const MovementData* rule = DataManager::GetMovementData(PLAYER_MOVEMENT_ID);   // 서버 시작 때 존재를 확인했다
	const char* reason = nullptr;
	if (player->GetStat().IsDead()) reason = "dead";
	else if (skill == nullptr || skill->slot == 0 || skill->aimMaxMs <= 0) reason = "not-aimable";
	else if (!player->IsSkillReady(skill->skillId)) reason = "cooldown";
	else if (!player->aim.onCooldown.IsReady()) reason = "too-frequent";

	if (reason != nullptr)
	{
		LOG_DEBUG("Aim", "거부 player={} slot={} seq={} reason={}", player->GetId(), slot, seq, reason);
		Metrics::AimReject.fetch_add(1);
		// 거부는 본인에게만(seq 포함 — 본인 클라이언트는 seq가 최신일 때만 끈다). 이미 조준 중이었다면 클라이언트가 끄게 되므로
		// 서버도 함께 해제하고 다른 사람에게 알린다.
		if (player->IsSkillAiming())
		{
			player->SetSkillAim(nullptr, nowMs);
			BroadcastAOI(static_pointer_cast<Character>(player), PacketSerializer::MAKE_SC_SKILL_AIM(player->GetId(), 0, false, 0));
		}
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_SKILL_AIM(player->GetId(), 0, false, seq));
		return;
	}

	player->aim.onCooldown.Reset(rule->aimMinIntervalMs);
	player->SetSkillAim(skill, nowMs);
	Metrics::AimOn.fetch_add(1);
	BroadcastAOI(static_pointer_cast<Character>(player), PacketSerializer::MAKE_SC_SKILL_AIM(player->GetId(), skill->skillId, true, 0), true);
}

void Room::UpdateAims(long long nowMs)
{
	for (const auto& [id, player] : _players)
	{
		if (!player->IsSkillAiming() || nowMs < player->aim.deadlineMs) continue;

		player->SetSkillAim(nullptr, nowMs);
		Metrics::AimTimeout.fetch_add(1);
		BroadcastAOI(static_pointer_cast<Character>(player), PacketSerializer::MAKE_SC_SKILL_AIM(id, 0, false, 0));
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_SKILL_AIM(id, 0, false, player->aim.seq));
	}
}

void Room::PlayerChangeEquip(shared_ptr<Player> player, Protocol::EquipPart part, int itemId)
{
	if (player == nullptr) return;

	auto reject = [&](Protocol::EquipResult result) {
		LOG_DEBUG("Equip", "거부 player={} part={} item={} result={}", player->GetId(), static_cast<int>(part), itemId, Protocol::EquipResult_Name(result));
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_CHANGE_EQUIP_RESULT(result, part, itemId));
	};

	if (GetGameObject(player->GetId()) == nullptr) { reject(Protocol::EQUIP_RESULT_NOT_IN_ROOM); return; }

	// 검증 순서: 형식(슬롯·아이템) → 상태(사망·시전) → 빈도. 빈도 제한은 통과한 요청만 시간을 갱신한다.
	if (part < Protocol::EQUIP_PART_HEAD || part > Protocol::EQUIP_PART_OFFHAND) { reject(Protocol::EQUIP_RESULT_WRONG_SLOT); return; }
	const ItemData* item = nullptr;
	if (itemId != 0)
	{
		item = DataManager::GetItemData(itemId);
		if (item == nullptr) { reject(Protocol::EQUIP_RESULT_NO_ITEM); return; }
		if (static_cast<int>(item->slot) != static_cast<int>(part)) { reject(Protocol::EQUIP_RESULT_WRONG_SLOT); return; }
	}
	// 보조손(방패)은 한손검을 든 상태에서만 착용한다 (MC4.2f, DECISIONS 2026-11-09). 해제는 언제나 허용.
	if (part == Protocol::EQUIP_PART_OFFHAND && itemId != 0 && player->GetWeaponType() != WeaponType::Sword) { reject(Protocol::EQUIP_RESULT_NEED_SWORD); return; }
	if (player->GetStat().IsDead()) { reject(Protocol::EQUIP_RESULT_DEAD); return; }
	if (player->IsCasting()) { reject(Protocol::EQUIP_RESULT_ACTING); return; }

	const MovementData* rule = DataManager::GetMovementData(PLAYER_MOVEMENT_ID);   // 서버 시작 때 존재를 확인했다
	if (!player->equipCooldown.IsReady()) { reject(Protocol::EQUIP_RESULT_TOO_FREQUENT); return; }
	player->equipCooldown.Reset(rule->equipMinIntervalMs);

	// 같은 아이템이면 바뀌는 것이 없다 (잠금·방송 없이 끝낸다)
	if (player->GetEquippedItem(part) == itemId) return;

	// 무기가 바뀌면 조준 중인 스킬도 바뀌므로 조준을 해제한다. 상태만 바꾼다 — 원격은 SC_CHANGE_EQUIP으로, 본인은 자기 무기 교체 처리로 끈다.
	if (part == Protocol::EQUIP_PART_WEAPON && player->IsSkillAiming())
		player->SetSkillAim(nullptr, chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count());

	player->SetEquippedItem(part, itemId);

	// 무기가 한손검이 아니게 되면(활·맨손 포함) 보조손(방패)을 서버가 비운다 (MC4.2f)
	const bool offhandCleared = part == Protocol::EQUIP_PART_WEAPON &&
		player->GetEquippedItem(Protocol::EQUIP_PART_OFFHAND) != 0 && player->GetWeaponType() != WeaponType::Sword;
	if (offhandCleared)
		player->SetEquippedItem(Protocol::EQUIP_PART_OFFHAND, 0);

	player->RecalcStat();

	// 무기 슬롯이 바뀌면 착용·해제 모두 행동 잠금 — 스왑으로 쿨타임을 우회하지 못하게 한다(DECISIONS 2026-11-04, 2026-11-07)
	if (part == Protocol::EQUIP_PART_WEAPON)
		player->LockActionForSwap(rule->weaponSwapLockMs);

	// 보조손 해제를 먼저 보내 클라이언트가 "활 + 방패" 상태를 거치지 않게 한다. 두 패킷 모두 최종 스탯을 싣는다.
	if (offhandCleared)
		BroadcastAOI(static_pointer_cast<Character>(player), PacketSerializer::MAKE_SC_CHANGE_EQUIP(player->GetId(), Protocol::EQUIP_PART_OFFHAND, 0, player->GetInfo().stat()), true);
	BroadcastAOI(static_pointer_cast<Character>(player), PacketSerializer::MAKE_SC_CHANGE_EQUIP(player->GetId(), part, itemId, player->GetInfo().stat()), true);

	// 변경 즉시 저장(DECISIONS 2026-11-07, 측정: TESTING.md MC3.3)
	RequestEquipSave(player);
}

void Room::RequestEquipSave(shared_ptr<Player> player)
{
	if (player == nullptr || player->GetPlayerUID() == 0) return;   // DB 캐릭터가 아닌 경우(로그인 전 등)
	Metrics::EquipSaveRequested.fetch_add(1);

	if (player->equipSaveInFlight)
	{
		player->equipSaveDirty = true;
		Metrics::EquipSaveCoalesced.fetch_add(1);
		return;
	}

	player->equipSaveInFlight = true;
	player->equipSaveDirty = false;
	Metrics::EquipSaveSent.fetch_add(1);
	Metrics::EquipSaveInFlight.fetch_add(1);

	vector<int32_t> items(player->GetInfo().equip_items().begin(), player->GetInfo().equip_items().end());
	GDBWorker->PushDBJob(&DatabaseWorker::SaveEquipSnapshot, shared_from_this(), player, player->GetPlayerUID(), items, Metrics::NowUs());
}

void Room::OnEquipSaved(shared_ptr<Player> player, bool success)
{
	Metrics::EquipSaveInFlight.fetch_sub(1);
	player->equipSaveInFlight = false;

	// 실패하면 dirty로 남겨 다음 변경·퇴장 때 다시 저장한다.
	if (!success)
	{
		player->equipSaveDirty = true;
		return;
	}
	// 저장하는 동안 다시 바뀌었으면 최신 값으로 한 번 더 저장한다.
	if (player->equipSaveDirty)
		RequestEquipSave(player);
}

void Room::Broadcast(shared_ptr<SendBuffer> sendBuffer)
{
	for (const auto& p : _players)
	{
		if (auto session = p.second->GetSession())
			session->Send(sendBuffer);
	}
}

void Room::BroadcastAOI(shared_ptr<Character> viewableObj, shared_ptr<SendBuffer> sendBuffer, bool sendToSelf)
{
	if (sendToSelf && viewableObj->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER) {
		shared_ptr<Player> player = static_pointer_cast<Player>(viewableObj);
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(sendBuffer);
	}


	vector<int>& currentView = viewableObj->_viewList;

	for (const int& id : currentView) {
		shared_ptr<Player> target_player = Id2Player(id);
		if (target_player == nullptr) continue;
		if (shared_ptr<Session> session = target_player->GetSession()) {
			session->Send(sendBuffer);
		}
	}
}

void Room::PlayerChat(shared_ptr<Player> player, wstring msg)
{
	if (player == nullptr) return;
	if (GetGameObject(player->GetId()) == nullptr) return;   // 이미 방을 나간 플레이어

	// 도배 방지: 간격 안에 다시 보낸 채팅은 버린다.
	if (!player->chatCooldown.IsReady()) return;
	player->chatCooldown.Reset(CHAT_MIN_INTERVAL_MS);

	// 내용은 PacketHandler::SanitizeChat에서 검증됐다. 방 전체(보낸 사람 포함)에 방송한다.
	Broadcast(PacketSerializer::MAKE_SC_CHAT(player->GetId(), player->GetName(), msg));
}

void Room::CastSkill(shared_ptr<Character> caster, int slot, int targetId, float aimYaw, float aimX, float aimY)
{
	if (caster == nullptr) return;
	if (GetGameObject(caster->GetId()) == nullptr) return;   // 이 방에 없는 시전자

	const bool casterIsPlayer = (caster->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER);

	// 스킬 조준 중이었다면 공격 요청을 받는 순간(검증 전) 해제한다(MC4.2d-1, DECISIONS 2026-11-09).
	// 본인 클라이언트는 공격을 보내면서 이미 껐다. 시전이 확정되면 SC_ATTACK이 다른 사람에게 해제를 뜻하고,
	// 아래 검증에서 실패해 돌아가면 소멸자가 다른 사람에게 false를 보낸다(조준 자세가 남지 않게).
	struct AimClearNotice
	{
		Room* room = nullptr;
		shared_ptr<Character> caster;
		bool pending = false;
		~AimClearNotice()
		{
			if (pending)
				room->BroadcastAOI(caster, PacketSerializer::MAKE_SC_SKILL_AIM(caster->GetId(), 0, false, 0));
		}
	} aimNotice{ this, caster, false };
	if (casterIsPlayer)
	{
		shared_ptr<Player> player = static_pointer_cast<Player>(caster);
		if (player->IsSkillAiming())
		{
			player->SetSkillAim(nullptr, chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count());
			Metrics::AimCastClear.fetch_add(1);
			aimNotice.pending = true;
		}
	}

	const SkillData* skill = caster->FindCastableSkill(slot);
	if (skill == nullptr) return;

	const Protocol::PositionInfo& pos = caster->GetPosition();
	constexpr float RAD_TO_DEG = 180.0f / MathUtils::PI;

	float yaw = aimYaw;
	float ax = aimX;
	float ay = aimY;

	if (skill->targeting == SkillTargeting::Targeted)
	{
		// 클라이언트가 보낸 대상 id는 신뢰하지 않고, 존재/생존/적대 관계/사거리를 서버에서 다시 확인한다.
		shared_ptr<GameObject> targetObj = GetGameObject(targetId);
		if (targetObj == nullptr) return;

		const Protocol::ObjectType wantedType = casterIsPlayer ? Protocol::ObjectType::OBJECT_TYPE_MONSTER : Protocol::ObjectType::OBJECT_TYPE_PLAYER;
		if (targetObj->GetType() != wantedType) return;
		if (static_pointer_cast<Character>(targetObj)->GetStat().IsDead()) return;

		const float dx = targetObj->GetPosition().x() - pos.x();
		const float dy = targetObj->GetPosition().y() - pos.y();
		const float dist = std::sqrt(dx * dx + dy * dy);
		if (dist > skill->castRange * 1.1f) return;   // 이동 지연을 감안한 10% 여유

		yaw = std::atan2(dy, dx) * RAD_TO_DEG;
	}
	else
	{
		if (!std::isfinite(yaw)) return;

		if (skill->hitShape == HitShape::Zone)
		{
			if (!std::isfinite(ax) || !std::isfinite(ay)) return;

			// 사거리를 넘는 지점은 사거리 끝으로 보정한다.
			float dx = ax - pos.x();
			float dy = ay - pos.y();
			const float dist = std::sqrt(dx * dx + dy * dy);
			if (dist > skill->castRange && dist > 0.f)
			{
				const float scale = skill->castRange / dist;
				dx *= scale;
				dy *= scale;
				ax = pos.x() + dx;
				ay = pos.y() + dy;
			}
			if (dist > 0.f) yaw = std::atan2(dy, dx) * RAD_TO_DEG;
		}
	}

	// 여기부터 시전이 확정된다. 조준 해제는 SC_ATTACK이 알린다.
	aimNotice.pending = false;
	caster->GetMutablePosition()->set_yaw(yaw);
	const Character::CastTiming timing = caster->BeginCast(*skill);
	if (casterIsPlayer)
		Metrics::AttackAccepted.fetch_add(1);

	BroadcastAOI(caster, PacketSerializer::MAKE_SC_ATTACK(caster->GetId(), skill->skillId, targetId, yaw, ax, ay, static_cast<int>(timing.cooldownMs)), true);

	// 피해 = 스킬 BaseDamage + 시전자 공격력. 시전 순간에 확정해 즉시 판정·투사체·장판에 같은 값을 쓴다
	// (판정 전에 무기를 바꿔도 이미 나간 공격은 그대로 — DESIGN_COMBAT_SKILL.md). 방어력 감소는 나중(Character::OnDamaged).
	const int damage = skill->baseDamage + caster->GetStat().GetAttackDamage();

	ReserveJob(static_cast<DWORD>(timing.hitDelayMs), &Room::ExecuteSkillHit, caster->GetId(), skill->skillId, targetId, yaw, ax, ay, damage);
}

void Room::ExecuteSkillHit(int attackerId, int skillId, int targetId, float aimYaw, float aimX, float aimY, int damage)
{
	auto attackerObj = GetGameObject(attackerId);
	if (attackerObj == nullptr) return;

	auto attackerChar = std::static_pointer_cast<Character>(attackerObj);

	CombatProcessor::ProcessSkillHit(shared_from_this(), attackerChar, skillId, targetId, aimYaw, aimX, aimY, damage);
}

void Room::CreateZone(shared_ptr<Character> attacker, const SkillData& skill, float x, float y, int damage)
{
	SkillZone zone;
	zone.attackerId = attacker->GetId();
	zone.x = x;
	zone.y = y;
	zone.radius = skill.radius;
	zone.damage = damage;
	zone.tickMs = max(1, skill.tickMs);
	zone.endTime = chrono::steady_clock::now() + chrono::milliseconds(skill.durationMs);
	_zones.push_back(std::move(zone));
	Metrics::ZonesCreated.fetch_add(1);
}

void Room::UpdateZones()
{
	if (_zones.empty()) return;

	const auto now = chrono::steady_clock::now();

	for (size_t i = 0; i < _zones.size();)
	{
		SkillZone& zone = _zones[i];

		shared_ptr<GameObject> attackerObj = GetGameObject(zone.attackerId);
		if (now >= zone.endTime || attackerObj == nullptr)
		{
			_zones[i] = std::move(_zones.back());
			_zones.pop_back();
			continue;
		}

		shared_ptr<Character> attacker = static_pointer_cast<Character>(attackerObj);
		const float radiusSq = zone.radius * zone.radius;

		// 시전자가 플레이어면 몬스터를, 몬스터면 플레이어를 맞힌다. 장판 안에 들어온 순간 바로 피해를 주고 이후 tickMs마다 반복한다.
		auto hitTargets = [&](auto& container)
		{
			for (auto& [id, target] : container)
			{
				if (target->GetStat().IsDead()) continue;

				const float dx = target->GetPosition().x() - zone.x;
				const float dy = target->GetPosition().y() - zone.y;
				if (dx * dx + dy * dy > radiusSq) continue;

				auto& nextHit = zone.nextHit[id];
				if (now < nextHit) continue;

				nextHit = now + chrono::milliseconds(zone.tickMs);
				Metrics::ZoneHits.fetch_add(1);
				target->OnDamaged(zone.damage, zone.attackerId, attacker);
			}
		};

		if (attacker->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER) hitTargets(_monsters);
		else hitTargets(_players);

		++i;
	}
}
void Room::ApplyDelayedDamage(int targetId, int attackerId, int damage)
{
	auto targetObj = GetGameObject(targetId);
	// 이미 발사된 투사체는 공격자가 날아가는 동안 사라졌어도(접속 종료 등) 피해를 준다. 이때 attackerObj는 nullptr다.
	auto attackerObj = GetGameObject(attackerId);

	if (targetObj == nullptr) return;

	auto targetChar = std::static_pointer_cast<Character>(targetObj);

	if (!targetChar->GetStat().IsDead())
	{
		LOG_DEBUG("Room", "Target[{}] Dmg: {}", targetId, damage);
		targetChar->OnDamaged(damage, attackerId, static_pointer_cast<Character>(attackerObj));
	}
}

void Room::NpcEnterRoom(shared_ptr<Monster> monster)
{
	if (false == AddObject(monster)) {
		return;
	}

	_monsters.emplace(monster->GetId(), monster);
	Metrics::Monsters.fetch_add(1);

	ViewUpdate result = _gameMap.EnterMap(monster->GetId(), monster->GetPosition());
	if (!result.entered.empty()) {
		UpdateView(monster, result);
	}
}

void Room::SpawnMonster(int spawnIndex)
{
	const auto& spawns = _gameMap.GetMonsterSpawns();
	if (spawnIndex < 0 || spawnIndex >= static_cast<int>(spawns.size())) return;

	const MonsterSpawnPoint& spawn = spawns[spawnIndex];

	// 맵 로직 JSON의 monsterTemplateId가 MonsterTable에 없으면 스탯 없는 몬스터가 생기므로 이 마커는 스폰하지 않는다.
	// (방은 플레이어가 늘면 실행 중에도 만들어지므로 서버를 멈추지 않고 ERROR만 남긴다)
	if (DataManager::GetMonsterData(spawn.MonsterTemplateId) == nullptr)
	{
		LOG_ERROR("Room", "스폰 마커 {} 의 monsterTemplateId {} 가 MonsterTable에 없어 스폰하지 않습니다", spawnIndex, spawn.MonsterTemplateId);
		return;
	}

	shared_ptr<Monster> monster = make_shared<Monster>(spawn.MonsterTemplateId);
	monster->SetId(MonsterIdGenerator());
	monster->SetSpawnIndex(spawnIndex);

	Protocol::PositionInfo spawnPos;
	spawnPos.set_x(spawn.Pos.X);
	spawnPos.set_y(spawn.Pos.Y);
	spawnPos.set_z(spawn.Pos.Z);
	spawnPos.set_yaw(spawn.Pos.Yaw);

	// 마커 반경 안의 임의 위치. 내비메시 위로 보정할 수 없으면 마커 위치를 그대로 쓴다.
	if (spawn.SpawnRadius > 0.f)
	{
		const float angle = (rand() / static_cast<float>(RAND_MAX)) * 2.f * MathUtils::PI;
		const float dist = sqrtf(rand() / static_cast<float>(RAND_MAX)) * spawn.SpawnRadius;

		Protocol::PositionInfo candidate = spawnPos;
		candidate.set_x(spawn.Pos.X + cosf(angle) * dist);
		candidate.set_y(spawn.Pos.Y + sinf(angle) * dist);

		if (NavmeshManager* nav = GetNavManager()) {
			if (nav->AdjustPositionToNavMesh(candidate))
				spawnPos = candidate;
		}
	}

	monster->SetPosition(spawnPos);
	NpcEnterRoom(monster);
}

void Room::RemoveMonster(int monsterId)
{
	shared_ptr<Monster> monster = Id2Monster(monsterId);
	if (monster == nullptr) return;

	const int spawnIndex = monster->GetSpawnIndex();
	const int templateId = monster->GetInfo().template_id();

	// 시야에 있던 플레이어들에게 SC_REMOVE_OBJECT가 전달된다.
	ViewUpdate result = _gameMap.LeaveMap(monsterId, monster->_viewList);
	if (!result.leaved.empty()) {
		UpdateView(monster, result);
	}

	RemoveObject(monsterId);
	_monsters.erase(monsterId);
	Metrics::Monsters.fetch_sub(1);

	// 사망 시점부터 RespawnSec가 지난 뒤 같은 마커에서 새로 만든다. (이미 deathRemoveDelay만큼 지났으므로 그만큼 뺀다)
	const MonsterData* data = DataManager::GetMonsterData(templateId);
	const int delayMs = data ? max(0, data->respawnMs - data->deathRemoveDelayMs) : 7000;
	ReserveJob(delayMs, &Room::SpawnMonster, spawnIndex);
}

void Room::NPCMove(shared_ptr<Monster> monster)
{
	if (GetGameObject(monster->GetId()) == nullptr)
		return;

	ViewUpdate result = _gameMap.UpdateMap(monster->GetId(), monster->GetPosition(), monster->_viewList);
	if (!result.entered.empty() || !result.leaved.empty()) {
		UpdateView(monster, result);
	}
}

void Room::UpdateView(shared_ptr<Character> subjectChar, const ViewUpdate& result)
{
	int subjectId = subjectChar->GetId();

	bool isSubjectPlayer = (subjectChar->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER);
	bool isSubjectMonster = (subjectChar->GetType() == Protocol::ObjectType::OBJECT_TYPE_MONSTER);

	shared_ptr<Player> subjectPlayer = isSubjectPlayer ? static_pointer_cast<Player>(subjectChar) : nullptr;
	shared_ptr<Monster> subjectMonster = isSubjectMonster ? static_pointer_cast<Monster>(subjectChar) : nullptr;

	shared_ptr<SendBuffer> removeSubjectBuffer = PacketSerializer::MAKE_SC_REMOVE_OBJECT(subjectId);
	shared_ptr<SendBuffer> addSubjectBuffer = PacketSerializer::MAKE_SC_ADD_OBJECT(subjectChar);
	
	// 시야에서 나간 객체 처리
	for (int targetId : result.leaved) {
		if (targetId == subjectId) continue;
		shared_ptr<GameObject> targetObj = GetGameObject(targetId);
		if (!targetObj) continue;

		Protocol::ObjectType targetType = targetObj->GetType();
		bool isTargetPlayer = (targetType == Protocol::ObjectType::OBJECT_TYPE_PLAYER);
		bool isTargetMonster = (targetType == Protocol::ObjectType::OBJECT_TYPE_MONSTER);

		if (isSubjectPlayer || (isSubjectMonster && isTargetPlayer))
		{
			subjectChar->RemoveView(targetId);

			if (isSubjectMonster && isTargetPlayer) {
				subjectMonster->SleepIfNoPlayer();
			}
			else if (isSubjectPlayer) {
				if (shared_ptr<Session> subjectSession = subjectPlayer->GetSession()) {
					subjectSession->Send(PacketSerializer::MAKE_SC_REMOVE_OBJECT(targetId));
				}
			}
		}

		if (isTargetPlayer || isTargetMonster)
		{
			shared_ptr<Character> targetChar = static_pointer_cast<Character>(targetObj);

			if (isTargetPlayer || (isTargetMonster && isSubjectPlayer))
			{
				targetChar->RemoveView(subjectId);

				if (isTargetMonster && isSubjectPlayer) {
					static_pointer_cast<Monster>(targetChar)->SleepIfNoPlayer(); 
				}
				else if (isTargetPlayer) {
					if (shared_ptr<Session> targetSession = static_pointer_cast<Player>(targetChar)->GetSession()) {
						targetSession->Send(removeSubjectBuffer);
					}
				}
			}
		}
	}	
	// 새로 시야에 들어온 객체 처리
	for (int targetId : result.entered) {
		if (targetId == subjectId) continue;
		shared_ptr<GameObject> targetObj = GetGameObject(targetId);
		if (!targetObj) continue;

		Protocol::ObjectType targetType = targetObj->GetType();
		bool isTargetPlayer = (targetType == Protocol::ObjectType::OBJECT_TYPE_PLAYER);
		bool isTargetMonster = (targetType == Protocol::ObjectType::OBJECT_TYPE_MONSTER);

		if (isSubjectPlayer || (isSubjectMonster && isTargetPlayer))
		{
			subjectChar->_viewList.push_back(targetId);

			if (isSubjectMonster && isTargetPlayer) {
				subjectMonster->WakeUpByPlayer(static_pointer_cast<Player>(targetObj));
			}
			else if (isSubjectPlayer) {
				if (shared_ptr<Session> subjectSession = subjectPlayer->GetSession()) {
					subjectSession->Send(PacketSerializer::MAKE_SC_ADD_OBJECT(targetObj));
				}
			}
		}

		if (isTargetPlayer || isTargetMonster)
		{
			shared_ptr<Character> targetChar = static_pointer_cast<Character>(targetObj);

			if (isTargetPlayer || (isTargetMonster && isSubjectPlayer))
			{
				targetChar->_viewList.push_back(subjectId);

				if (isTargetMonster && isSubjectPlayer) {
					static_pointer_cast<Monster>(targetChar)->WakeUpByPlayer(subjectPlayer); 
				}
				else if (isTargetPlayer) {
					if (shared_ptr<Session> targetSession = static_pointer_cast<Player>(targetChar)->GetSession()) {
						targetSession->Send(addSubjectBuffer);
					}
				}
			}
		}
	}
}

shared_ptr<Player> Room::Id2Player(int pId)
{
	auto it = _players.find(pId);
	if (it == _players.end())
		return nullptr;

	return it->second;
}

shared_ptr<Monster> Room::Id2Monster(int mId)
{
	auto it = _monsters.find(mId);
	if (it == _monsters.end())
		return nullptr;

	return it->second;
}

shared_ptr<Room> RoomManager::CreateRoom()
{
	int id = IdGenerator();

	shared_ptr<Room> room = make_shared<Room>(GTimer, _iocpHandle);
	// 맵·몬스터 로딩이 오래 걸리므로 매니저 락을 잡지 않고 먼저 초기화한 뒤 등록한다.
	room->InitRoom();

	{
		RWLock::WriteGuard lock(_lock);
		_rooms.insert({ id, room });
	}

	return room;
}

void RoomManager::Remove(int roomId)
{
	RWLock::WriteGuard lock(_lock);
	_rooms.erase(roomId);
}

void RoomManager::Remove(shared_ptr<Room> room)
{
	return;
}

bool RoomManager::TryEnterExisting(shared_ptr<Player> player)
{
	RWLock::ReadGuard lock(_lock);
	for (auto& [id, room] : _rooms) {
		if (room->TryReserveSlot()) {
			player->SetOwnerRoom(room);
			room->PushJob(&Room::PlayerEnterRoom, player);
			return true;
		}
	}
	return false;
}

void RoomManager::EnterPlayer(shared_ptr<Player> player)
{
	if (player == nullptr) return;

	if (TryEnterExisting(player))
		return;

	// 방이 없으면 하나만 만든다. 락을 기다리는 동안 다른 스레드가 만든 방이 있으면 그 방을 쓴다.
	lock_guard<mutex> createLock(_createMutex);
	if (TryEnterExisting(player))
		return;

	shared_ptr<Room> newRoom = CreateRoom();
	newRoom->TryReserveSlot();
	player->SetOwnerRoom(newRoom);
	newRoom->PushJob(&Room::PlayerEnterRoom, player);
}


int RoomManager::IdGenerator()
{
	static atomic<int> _idGenerator = 0;
	return ++_idGenerator;
}
