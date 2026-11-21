#include "pch.h"
#include "Game/Room/Room.h"
#include "Network/Session.h"
#include "Packet/PacketHandler.h"
#include "Game/Object/Player.h"
#include "Game/Object/Monster.h"
#include "Game/Object/DropItem.h"
#include "Game/Object/TownNpc.h"
#include "Game/Combat/CombatProcessor.h"
#include "Data/ServerData.h"
#include "Utils/Metrics.h"
#include "DB/DatabaseWorker.h"
#include "Game/Auction/AuctionService.h"

Room::Room(shared_ptr<Timer> timer, HANDLE iocpHandle, RoomInfo info) : _info(std::move(info)), _timer(timer), _jobQueue(make_shared<JobQueue>(iocpHandle))
{
	s_alive.fetch_add(1);
}
Room::~Room()
{
	s_alive.fetch_sub(1);
	LOG_DEBUG("Room", "Room {} 소멸", _info.roomId);
}

void Room::Close()
{
	if (_closed.load()) return;

	// 닫기 전에 비어 있어야 한다(WorldService가 안에 있는 사람·들어오는 사람이 0일 때만 닫는다). 남아 있으면 그 접속을 끊는다
	// — 퇴장 잡은 닫힌 방에서도 평소대로 돌아 저장·정리 신호까지 간다.
	if (!_players.empty() || !_loading.empty())
	{
		LOG_ERROR("Room", "Room {}: 플레이어 {}명(로딩 대기 {}명)이 남은 채 닫습니다 — 접속을 끊습니다", _info.roomId, _players.size(), _loading.size());
		for (const auto& [id, player] : _players)
			if (shared_ptr<Session> session = player->GetSession())
				session->ForceClose(L"Room: closed with players inside");
		for (const auto& [id, entry] : _loading)
			if (shared_ptr<Session> session = entry.player->GetSession())
				session->ForceClose(L"Room: closed with players inside");
	}

	_closed.store(true);

	// 몬스터·드랍·장판·NPC를 버린다. 보고 있는 사람이 없으므로 SC_REMOVE_OBJECT는 보내지 않는다. 전역 지표는 여기서 맞춘다.
	const size_t monsterCount = _monsters.size(), dropCount = _drops.size();
	Metrics::Monsters.fetch_sub(static_cast<int>(monsterCount));
	Metrics::Drops.fetch_sub(static_cast<int>(dropCount));
	for (auto it = _objects.begin(); it != _objects.end();)
	{
		if (_monsters.count(it->first)) it = _objects.erase(it);
		else ++it;
	}
	_monsters.clear();
	_drops.clear();
	_dropOrder.clear();
	_zones.clear();
	_npcs.clear();

	// 잡 큐의 보낼 세션 목록을 모두 내보낸다(Server/CLAUDE.md "송신 묶기" 주의 ②) — 방금 이 방을 떠난 세션이 목록에 남아 있을 수 있다.
	_jobQueue->CloseSendBatch();

	GRoomManager->Remove(_info.roomId);
	LOG_INFO("Room", "Room {}(채널 {}, {} 구역 '{}') 닫음: 남아 있던 몬스터 {}, 드랍 {}", _info.roomId, _info.channel, _info.levelName, _info.area, monsterCount, dropCount);
}

bool Room::InitRoom(bool allowBroken)
{
	// 레벨 수치(부활·포털 재사용 대기 등)는 이 Room 레벨의 WorldTable 행에서 읽는다. 행이 없으면 Room을 돌릴 수 없다.
	_world = DataManager::GetWorldData(_info.levelName);
	if (_world == nullptr)
	{
		LOG_ERROR("Room", "Room {}: WorldTable에 맵 '{}' 행이 없습니다", _info.roomId, _info.levelName);
		return false;
	}
	const bool mapOk = _gameMap.LoadMapData(_info.levelName, _info.area);
	if (!mapOk)
	{
		LOG_ERROR("Room", "Room {}(채널 {}, 맵 '{}'): 맵 초기화(LoadMapData) 중 일부 문제가 발생했습니다.", _info.roomId, _info.channel, _info.levelName);
		if (!allowBroken) return false;
	}
	_gameMap.Init(weak_from_this());

	// 송신 묶기(P1b, D-206): 방 작업 안의 Send는 잡 큐가 모아 세션당 WSASend 1회로 보낸다(JobQueue.h).
	// 큐가 비었는데 아직 보낼 때가 안 된 세션이 남으면 그 시각에 빈 작업을 예약해 깨운다.
	_jobQueue->SetSendBatch(GServerConfig.sendFlushIntervalMs, [weakRoom = weak_from_this()](int delayMs) {
		if (shared_ptr<Room> room = weakRoom.lock())
			room->ReserveJob(static_cast<DWORD>(delayMs), &Room::OnSendWake, Metrics::NowUs() + delayMs * 1000LL);
	});

	// 이 Room 구역의 시작 지점(DG3b). 구역에 시작 지점이 없으면(사냥터) 비어 있다.
	for (int i = 0; i < static_cast<int>(_gameMap.GetPlayerStarts().size()); ++i)
		if (_gameMap.InArea(_gameMap.GetPlayerStarts()[i].Area)) _startIndices.push_back(i);

	// 마커는 레벨 전체가 들어 있다 — 이 Room의 구역 것만 쓴다(GameMap::InArea).
	const auto& monsterSpawns = _gameMap.GetMonsterSpawns();
	int areaSpawnCount = 0;
	for (const MonsterSpawnPoint& spawn : monsterSpawns)
		if (_gameMap.InArea(spawn.Area)) ++areaSpawnCount;
	if (areaSpawnCount == 0)
	{
		// 구역으로 나뉜 레벨의 마을 Room은 스폰이 없는 것이 정상이다.
		if (_info.area.empty())
			LOG_ERROR("Room", "스폰 포인트가 없어 몬스터를 생성할 수 없습니다!");
		else
			LOG_INFO("Room", "Room {}(채널 {}, {} 구역 '{}'): 몬스터 스폰 없음", _info.roomId, _info.channel, _info.levelName, _info.area);
	}
	else
	{
		// 구역 이름이 있는 마커는 SpawnTable의 (맵, 구역) 행이 종류·수·리스폰을 정한다(L3, D-194). 한 구역에 여러 행이면 지점마다 행별 수만큼 섞어 놓는다.
		// 구역 이름이 없는 마커(옛 맵)는 마커의 monsterTemplateId·maxCount를 그대로 쓴다.
		std::map<string, int> zoneCounts;      // 구역 → 놓은 수 (시작 로그용)
		std::set<string> missingZones;
		for (int i = 0; i < static_cast<int>(monsterSpawns.size()); ++i) {
			const MonsterSpawnPoint& spawn = monsterSpawns[i];
			if (!_gameMap.InArea(spawn.Area)) continue;
			if (spawn.ZoneName.empty())
			{
				for (int n = 0; n < max(1, spawn.MaxCount); ++n)
					SpawnMonster(i, spawn.MonsterTemplateId, 0);
				zoneCounts["(마커 값)"] += max(1, spawn.MaxCount);
				continue;
			}
			const vector<SpawnData>& rows = DataManager::GetSpawns(_info.levelName, spawn.ZoneName);
			if (rows.empty())
			{
				// 방은 플레이어가 늘면 실행 중에도 만들어지므로 서버를 멈추지 않고 ERROR만 남긴다(구역마다 한 번).
				if (missingZones.insert(spawn.ZoneName).second)
					LOG_ERROR("Room", "스폰 마커의 구역 '{}' 이 SpawnTable(MapName '{}')에 없어 그 구역은 스폰하지 않습니다", spawn.ZoneName, _info.levelName);
				continue;
			}
			for (const SpawnData& row : rows)
			{
				for (int n = 0; n < row.countPerSpawn; ++n)
					SpawnMonster(i, row.monsterTemplateId, row.respawnMs);
				zoneCounts[spawn.ZoneName] += row.countPerSpawn;
			}
		}
		string summary;
		for (const auto& [zone, count] : zoneCounts)
			summary += (summary.empty() ? "" : ", ") + zone + " " + std::to_string(count);
		LOG_INFO("Room", "Room {}(채널 {}, {} 구역 '{}') 몬스터 스폰: 지점 {}곳, 몬스터 {}마리 ({})", _info.roomId, _info.channel, _info.levelName, _info.area, areaSpawnCount, _monsters.size(), summary);
	}
	
	// 마을 NPC(L4): Npc 마커 + NpcTable
	SpawnTownNpcs();

	// 포털 목록(L5): 도착지가 정해지는 포털만 모아 패킷을 한 번 만들어 둔다.
	{
		// 이 Room 구역에 있고 도착지가 이 서버 안(같은 Room, 같은 레벨의 다른 구역 Room, WorldTable에 있는 다른 레벨 — DG3c)인 포털만.
		vector<PortalTriggerPoint> usable;
		vector<Protocol::PortalKind> kinds;
		Protocol::PositionInfo unusedPos;
		string unusedArea;
		for (const PortalTriggerPoint& portal : _gameMap.GetPortalTriggers())
		{
			if (!_gameMap.InArea(portal.Area)) continue;
			const PortalRoute route = ResolvePortalTarget(portal, unusedPos, unusedArea);
			if (route == PortalRoute::SameRoom || route == PortalRoute::OtherRoom)
			{
				usable.push_back(portal);
				kinds.push_back(Protocol::PORTAL_KIND_NORMAL);
			}
			else if (route == PortalRoute::OtherLevel)
			{
				const WorldData* target = DataManager::GetWorldData(portal.TargetMap);
				if (target == nullptr) continue;   // 이 서버가 띄우지 않는 레벨(정거장)
				usable.push_back(portal);
				kinds.push_back(target->isDungeon ? Protocol::PORTAL_KIND_DUNGEON_ENTER
					: (_info.kind == RoomKind::Dungeon ? Protocol::PORTAL_KIND_DUNGEON_EXIT : Protocol::PORTAL_KIND_NORMAL));
			}
		}
		if (!usable.empty())
			_portalListBuffer = PacketSerializer::MAKE_SC_PORTAL_LIST(usable, kinds);
		if (_info.kind == RoomKind::Dungeon && _bossAlive == 0)
			LOG_ERROR("Room", "Room {}: 던전 레벨 '{}' 에 우두머리 스폰(구역 '{}')이 없어 클리어할 수 없습니다", _info.roomId, _info.levelName, ZONE_BOSS);
		LOG_INFO("Room", "Room {} 포털: 마커 {}개 중 쓸 수 있는 것 {}개, 시작 지점 {}곳", _info.roomId, _gameMap.GetPortalTriggers().size(), usable.size(), _startIndices.size());
	}

	long long currentTick = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	Update(currentTick);
	return true;   // 맵을 읽지 못했어도 여기까지 왔으면(allowBroken) 방은 돌고 있다
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

void Room::OnSendWake(long long dueUs)
{
	Metrics::SendWakeLate.Add(max<long long>(0, Metrics::NowUs() - dueUs));
}

void Room::Update(long long elapsedTime)
{
	if (_closed.load()) return;   // 닫힌 방은 틱을 다시 예약하지 않는다(DG3c)
	long long currentTick = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	ReserveJob(100, &Room::Update, currentTick);

	float dt = (currentTick - elapsedTime) / 1000.f;
	// 예정 간격(100ms)보다 얼마나 늦게 틱이 돌았는지 기록한다. 첫 틱(InitRoom에서 직접 호출)은 제외한다.
	if (elapsedTime != currentTick)
	{
		const long long lateUs = max<long long>(0, (currentTick - elapsedTime - 100) * 1000);
		Metrics::TickLate.Add(lateUs);
		Metrics::TickLateByKind[static_cast<int>(_info.kind)].Add(lateUs);
	}

	// 서버가 멈췄다 재개되는 등 틱 간격이 비정상이면 한 번에 큰 이동이 일어나지 않게 기본 간격으로 대체한다.
	if (dt <= 0.f || dt > 0.5f) dt = 0.1f;

	const auto bodyStart = chrono::steady_clock::now();
	for (const auto& object : _objects) {
		object.second->Update(dt);
	}
	UpdateZones();
	UpdateAims(currentTick);
	{
		const long long bodyUs = chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - bodyStart).count();
		Metrics::TickBody.Add(bodyUs);
		Metrics::TickBodyByKind[static_cast<int>(_info.kind)].Add(bodyUs);
	}

	// 1초(10틱)마다 플레이어별 시야 인원을 표본으로 남긴다(지표용, 틱 본문 시간에는 넣지 않음).
	if (++_tickCount % 10 == 0)
	{
		if (GServerConfig.viewAudit) AuditViews();
		UpdateSaves(currentTick);
		UpdateTrades(currentTick);
		if (!_loading.empty()) UpdateLoading(currentTick);

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

	// 플레이어의 자리는 부르는 쪽이 먼저 정한다(처음 입장 = PlaceNewPlayer, Room 이동 = PlayerArriveRoom — DG3b).
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
	// 입장 배정(WorldService::EnterWorld)과 이 잡 사이에 세션이 끊겼으면 들이지 않는다. 끊긴 세션의 OnDisconnected는
	// 아직 방에 없던 플레이어라 퇴장 잡을 넣지 않았을 수 있으므로, 여기서 들이면 유령 플레이어가 된다(감사 F1·F2).
	shared_ptr<GameSession> enterSession = player->GetSession();
	if (enterSession == nullptr || false == enterSession->IsConnected()) {
		LOG_DEBUG("Room", "Player[{}] Enter 취소: 세션 끊김", player->GetId());
		NotifyClosedIfDone(player);
		return;
	}

	PlaceNewPlayer(player);
	if (false == AddObject(player)) {
		// 같은 id가 이미 방에 있다(세션 id는 유일해 일어나지 않아야 한다). 어느 방에도 없는 채 접속만 남지 않게 끊는다 — 퇴장 잡이 정리 신호를 보낸다.
		enterSession->ForceClose(L"Room: AddObject failed");
		return;
	}

	if (_players.emplace(player->GetId(), player).second)
		_playerCount.fetch_add(1);
	player->roomMove = {};

	// 이동 검증 상태: 예산 0에서 시작해 시간이 지나면 모인다.
	player->moveGuard = {};
	player->moveGuard.lastAccrueMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	// 조준 상태도 새로 시작한다(seq는 클라이언트 값이라 그대로 둔다).
	player->SetSkillAim(nullptr, player->moveGuard.lastAccrueMs);
	player->deadAtMs = 0;
	player->portalCooldown.Reset(0);

	// 주기 저장 시각(A2.5): 입장 시각 + 간격에 id로 흩어 같은 틱에 몰리지 않게 한다.
	const long long saveIntervalMs = static_cast<long long>(GServerConfig.playerSaveIntervalSec) * 1000;
	player->nextProgressSaveMs = player->moveGuard.lastAccrueMs + saveIntervalMs + (player->GetId() % GServerConfig.playerSaveIntervalSec) * 1000LL;
	player->progressSaveRetries = 0;

	LOG_DEBUG("Room", "Client[{}] Enter Room", player->GetId());

	enterSession->Send(PacketSerializer::MAKE_SC_ADD_OBJECT(player));

	// 포털 목록(L5): 지금 쓸 수 있는(도착지가 정해지는) 포털만 본인에게 한 번. 목록은 InitRoom에서 만들어 둔다.
	if (_portalListBuffer != nullptr)
		enterSession->Send(_portalListBuffer);

	// 인벤토리(A3.3): 본인에게 전체를 한 번 보낸다. 더미 봇은 아이템 요청이 모두 거부되므로 보내지 않는다(D-164).
	if (!player->isDummy)
	{
		enterSession->Send(PacketSerializer::MAKE_SC_INVENTORY(player->inventory, player->GetGold(), player->quickSlots));
		// 로그인 때 비운 액티브 칸(쓸 수 없는 종류)이 있으면 DB에서도 지운다.
		RequestQuickSlotSave(player);

		// 로그인 때 메모리에서 인벤토리로 내린 장착 행을 DB에도 반영한다. 끝날 때까지 이 플레이어의 다른 아이템 요청은 BUSY.
		if (!player->pendingEquipFix.empty())
		{
			DB_ItemOp fix;
			fix.kind = ItemOpKind::LoginFix;
			for (const auto& [dbId, slot] : player->pendingEquipFix)
			{
				fix.demoteDbIds.push_back(dbId);
				fix.demoteSlots.push_back(slot);
			}
			StartItemOp(player, fix);
		}
	}

	// 맵 등록 및 시야 동기화
	ViewUpdate result = _gameMap.EnterMap(player->GetId(), player->GetPosition());
	if (!result.entered.empty()) {
		UpdateView(player, result);
	}

	// 주변 바닥 드랍(A3.4)
	player->dropView.clear();
	UpdateDropView(player);

	// 주변 마을 NPC(L4)
	player->npcView.clear();
	UpdateNpcView(player);
}

void Room::PlayerLeaveRoom(shared_ptr<Player> player)
{
	LOG_DEBUG("Room", "Player[{}] Leave", player->GetId());

	int playerId = player->GetId();

	// 입장 잡보다 퇴장 잡이 먼저 실행됐거나(입장 잡은 세션 끊김을 보고 취소됨) 중복 퇴장이면 정리할 것이 없다.
	if (nullptr == Id2Player(playerId)) {
		// Room 이동으로 이미 다른 Room에 넘어갔다면(퇴장 잡이 옛 Room 큐에 들어온 경우) 그 Room이 정리한다 — 이 플레이어의 필드는 소유 Room 잡에서만 만진다(DG3b).
		// 도착 Room 큐에는 들어오기 잡이 먼저 들어 있어, 정리는 그 뒤에 그 Room 스레드 한 곳에서 일어난다.
		const shared_ptr<Room> owner = player->GetCurrentRoom();
		if (owner != nullptr && owner.get() != this)
		{
			LOG_DEBUG("Room", "Player[{}] Leave: Room {} 로 넘어감 → 퇴장 잡 전달", playerId, owner->GetRoomId());
			owner->PushJob(&Room::PlayerLeaveRoom, player);
			return;
		}
		// 로딩 대기 중에 끊겼다(DG3c): 목록에서 빼고, 떠난 Room이 하지 않은 마지막 저장을 여기서 요청한다.
		if (const auto loadingIt = _loading.find(playerId); loadingIt != _loading.end() && loadingIt->second.player == player)
		{
			LOG_DEBUG("Room", "Player[{}] Leave: 로딩 대기 중 끊김", playerId);
			_loading.erase(loadingIt);
			Metrics::RoomMoveCanceled.fetch_add(1);
			RequestProgressSave(player);
			RequestQuickSlotSave(player);
			NotifyClosedIfDone(player);
			return;
		}
		LOG_DEBUG("Room", "Player[{}] Leave: 방에 없음(입장 취소·중복 퇴장)", playerId);
		NotifyClosedIfDone(player);
		return;
	}

	// 대기·이동 표시 중에 끊겼으면 이동을 취소한다(서비스의 응답은 "방에 없음"을 보고 버려진다).
	if (player->roomMove.Active())
	{
		Metrics::RoomMoveCanceled.fetch_add(1);
		player->roomMove = {};
	}
	player->ejectPending = false;

	ViewUpdate result = _gameMap.LeaveMap(playerId, player->_viewList);
	if (!result.leaved.empty()) {
		UpdateView(player, result);
	}


	if (false == RemoveObject(playerId)) {
		LOG_ERROR("Room", "LeaveRoom: 방에 없는 플레이어 id {}", playerId);
	}

	if (_players.erase(playerId) > 0)
		_playerCount.fetch_sub(1);

	// 거래 중이었다면 취소한다(A4.1). 확정 중(DB 결과 대기)이면 그대로 두고 결과 처리가 남은 쪽에만 반영한다.
	CancelTradeOf(player, Protocol::TRADE_RESULT_DISCONNECTED);

	// 드랍 시야는 목록만 비운다(나가는 클라이언트에는 보낼 것이 없다).
	player->dropView.clear();
	player->npcView.clear();

	// 조준 중이었다면 상태만 지운다(SC_REMOVE_OBJECT로 다른 사람의 표시는 사라진다).
	if (player->IsSkillAiming())
		player->SetSkillAim(nullptr, chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count());

	// 나갈 때의 진행 상황을 저장한다(A2.5). 진행 중인 저장이 있으면 OnProgressSaved가 이어서 최신 값으로 저장한다.
	RequestProgressSave(player);
	// 액티브 칸도 저장에 실패해 남은 것이 있으면 다시 보낸다(A3.6).
	RequestQuickSlotSave(player);

	// 보낸 DB 작업이 없으면 바로, 있으면 결과가 올 때(OnItemOpDone·OnProgressSaved·OnQuickSlotsSaved) 접속 목록에서 빼게 한다.
	NotifyClosedIfDone(player);
}

void Room::NotifyClosedIfDone(const shared_ptr<Player>& player)
{
	if (player == nullptr) return;
	if (player->closedNotified) return;

	// 아직 방에 있다(같은 id를 다른 플레이어가 다시 쓸 수 있으므로 포인터로 비교).
	const auto it = _players.find(player->GetId());
	if (it != _players.end() && it->second == player) return;

	// 접속이 살아 있다(입장에 실패해 방 밖에 있는 경우 등). 끊길 때 퇴장 잡이 다시 부른다.
	const shared_ptr<GameSession> session = player->GetSession();
	if (session != nullptr && session->IsConnected()) return;

	// DB에 가 있는 작업이 남았다. 결과를 받는 쪽이 다시 부른다.
	if (player->HasUnfinishedWork()) return;

	// 접속자 명부·채널 인원은 WorldService가 정리하고(더미 봇 포함), 중복 접속 목록(_online)은 서비스가 로비에 이어서 알린다(DG3a).
	if (player->closedNotified.exchange(true)) return;
	GWorld->PushJob(&WorldService::OnPlayerClosed, player->GetId(), player->GetPlayerUID(), player->isDummy);
}

void Room::SetPlayerParty(shared_ptr<Player> player, int partyId, uint32_t seq)
{
	if (player == nullptr) return;

	// Player의 필드는 소유 Room 잡에서만 만진다(DG3b). 서비스가 알림을 넣은 뒤 Room을 옮겼으면 그 Room으로 넘긴다.
	const shared_ptr<Room> owner = player->GetCurrentRoom();
	if (owner != nullptr && owner.get() != this)
	{
		owner->PushJob(&Room::SetPlayerParty, player, partyId, seq);
		return;
	}

	// 넘기는 동안 더 새 알림이 먼저 적용됐으면 버린다.
	if (seq <= player->partySeq) return;
	player->partySeq = seq;
	player->partyId = partyId;
	LOG_DEBUG("Party", "Player[{}] partyId={} (Room {})", player->GetId(), partyId, _info.roomId);
}

void Room::PlayerMove(shared_ptr<Player> player, Protocol::PositionInfo position, uint32_t correctionSeq)
{
	if (nullptr == Id2Player(player->GetId()))
		return;
	// Room 이동 표시 중의 요청은 버린다(DG3b). 도착하면 SC_ROOM_CHANGE의 위치·보정 순번으로 다시 맞춰진다.
	if (player->roomMove.transfer)
		return;

	// 사망 중 이동은 조용히 버린다(A2.4, D-149). 보정을 보내지 않는다 — 클라이언트도 입력을 막고, 부활 때 SC_REVIVE로 위치를 맞춘다.
	if (player->GetStat().IsDead())
	{
		Metrics::MoveDroppedDead.fetch_add(1);
		return;
	}

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
	// 이동 잠금 중에는 패킷마다 인정하는 여유(ServerMoveSlackCm)를 주지 않는다. 잠금 시작 때 남긴 예산(ServerMoveLockSlackCm)이 전부다.
	const float slack = player->IsMoveLocked(nowMs) ? 0.f : rule->moveSlackCm;
	if (dist > guard.budgetCm + slack)
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

	UpdateDropView(player);
	UpdateNpcView(player);

	// 거래 중이면 상대와의 거리를 본다(A4.1): TradeRangeCm을 넘으면 취소. 거리 안 이동은 허용한다. 확정 중이면 DB 결과를 기다린다.
	if (player->tradeId != 0)
	{
		const TradeSession* trade = FindTrade(player);
		if (trade != nullptr && trade->phase != Protocol::TRADE_PHASE_CONFIRMING && !IsTradeRangeOk(*trade))
			EndTrade(trade->id, Protocol::TRADE_RESULT_TOO_FAR);
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
	if (player->roomMove.transfer) return;   // Room 이동 표시 중(DG3b)

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

void Room::PlayerRevive(shared_ptr<Player> player)
{
	if (player == nullptr || Id2Player(player->GetId()) == nullptr) return;
	if (player->roomMove.Active()) return;   // 이미 이동 대기·이동 중(중복 부활 요청 포함 — DG3b)

	const WorldData* world = _world;
	const long long nowMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();

	// 검증: 죽어 있는가 → 대기 시간이 지났는가. 살아 있을 때 온 요청(중복 부활 포함)과 이른 요청은 응답 없이 버린다.
	const char* reason = nullptr;
	if (!player->GetStat().IsDead()) reason = "alive";
	else if (nowMs - player->deadAtMs < static_cast<long long>(world->reviveDelaySec) * 1000) reason = "too-early";
	if (reason != nullptr)
	{
		LOG_DEBUG("Revive", "거부 player={} reason={}", player->GetId(), reason);
		Metrics::ReviveReject.fetch_add(1);
		return;
	}

	// 사냥터에서 죽으면 같은 채널의 마을 Room에서 부활한다(D-216): 죽은 채로 Room을 옮기고, HP 회복과 SC_REVIVE는 도착 Room이 한다(PlayerArriveRoom).
	// 이 방의 사람들에게는 떠날 때 SC_REMOVE_OBJECT만 간다.
	if (_info.kind == RoomKind::Field && !_info.area.empty())
	{
		MoveRequest request;
		request.area = AREA_TOWN;
		BeginMove(player, Player::MoveReason::Revive, request, nullptr);
		return;
	}

	// 위치 = 구역이 있는 Room(마을)은 새로 고른 시작 지점, 구역이 없는 Room은 입장 때 배치된 자리. HP = 최대치 × ReviveHpPercent.
	player->GetStat().Revive(world->reviveHpPercent);
	player->ClearActionLocks();
	player->deadAtMs = 0;
	if (_info.area.empty())
		player->SetPosition(player->spawnPosition);
	else
	{
		Protocol::PositionInfo startPos = PickStartPosition();
		player->SetPosition(startPos);
		player->spawnPosition = startPos;
	}

	// 죽기 전·죽은 동안 보낸 이동이 뒤늦게 와도 버려지도록 보정 순번을 올리고, 이동 예산은 0에서 다시 모은다.
	Player::MoveGuard& guard = player->moveGuard;
	++guard.correctionSeq;
	guard.budgetCm = 0.f;
	guard.lastAccrueMs = nowMs;

	Metrics::Revives.fetch_add(1);
	LOG_DEBUG("Revive", "player={} hp={} pos=({:.0f},{:.0f},{:.0f})", player->GetId(), player->GetStat().GetHp(),
		player->GetPosition().x(), player->GetPosition().y(), player->GetPosition().z());

	// 죽은 자리에서 보던 사람들에게 먼저 알리고(본인 포함), 그 다음 시야를 새 위치 기준으로 갱신한다.
	// 새 위치에서 처음 보게 된 사람은 UpdateView의 SC_ADD_OBJECT(hp > 0)로 살아 있는 모습을 받는다.
	BroadcastAOI(static_pointer_cast<Character>(player), PacketSerializer::MAKE_SC_REVIVE(player, guard.correctionSeq), true);

	ViewUpdate result = _gameMap.UpdateMap(player->GetId(), player->GetPosition(), player->_viewList);
	if (!result.entered.empty() || !result.leaved.empty())
		UpdateView(player, result);

	UpdateDropView(player);
	UpdateNpcView(player);
}

PortalRoute Room::ResolvePortalTarget(const PortalTriggerPoint& portal, Protocol::PositionInfo& outPos, string& outArea) const
{
	// 다른 레벨로 가는 포털(던전 입구·던전 출구·정거장). 도착 Room과 자리는 서비스와 도착 Room이 정한다(DG3c) — 받을지는 부르는 쪽이 WorldTable로 본다.
	if (portal.TargetMap != _info.levelName) return PortalRoute::OtherLevel;

	// 도착 마커는 다른 구역에 있을 수 있다(마커 목록은 레벨 전체).
	const PortalTriggerPoint* target = _gameMap.FindPortal(portal.TargetPointId);
	if (target == nullptr || target->PointId == portal.PointId) return PortalRoute::None;

	outPos.set_x(target->Pos.X);
	outPos.set_y(target->Pos.Y);
	outPos.set_z(target->Pos.Z);
	outArea = target->Area;
	return _gameMap.InArea(target->Area) ? PortalRoute::SameRoom : PortalRoute::OtherRoom;
}

void Room::PlayerUsePortal(shared_ptr<Player> player, int portalId)
{
	if (player == nullptr || Id2Player(player->GetId()) == nullptr) return;
	if (player->roomMove.Active()) return;   // 이미 이동 대기·이동 중(같은 요청이 또 옴 — DG3b)

	const WorldData* world = _world;
	const MovementData* rule = DataManager::GetMovementData(PLAYER_MOVEMENT_ID);
	const long long nowMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();

	// 검증: 생존 → 시전·이동 잠금 아님 → 재사용 대기 → 포털 존재 → 도착지 → 거리.
	// 거리는 서버가 아는 내 위치 기준이고, 이동 패킷 사이에 벌어지는 만큼(ServerMoveSlackCm + ServerNavToleranceCm)을 더 받아 준다.
	const PortalTriggerPoint* portal = nullptr;
	Protocol::PositionInfo targetPos;
	string targetArea;
	PortalRoute route = PortalRoute::None;
	const char* reason = nullptr;
	bool unavailable = false;
	if (player->GetStat().IsDead()) reason = "dead";
	else if (player->IsCasting() || player->IsMoveLocked(nowMs)) reason = "acting";
	else if (!player->portalCooldown.IsReady()) reason = "too-frequent";
	else if ((portal = _gameMap.FindPortal(portalId)) == nullptr || !_gameMap.InArea(portal->Area)) reason = "no-portal";
	else if ((route = ResolvePortalTarget(*portal, targetPos, targetArea)) == PortalRoute::None) reason = "no-target";
	else if (route == PortalRoute::OtherLevel && DataManager::GetWorldData(portal->TargetMap) == nullptr) { reason = "no-target"; unavailable = true; }   // 이 서버가 띄우지 않는 레벨(정거장)
	else
	{
		const float dx = player->GetPosition().x() - portal->Pos.X, dy = player->GetPosition().y() - portal->Pos.Y;
		const float range = portal->TriggerRadius + rule->moveSlackCm + rule->navToleranceCm;
		if (dx * dx + dy * dy > range * range && !GServerConfig.portalNoRange) reason = "too-far";
	}
	if (reason != nullptr)
	{
		LOG_DEBUG("Portal", "거부 player={} portal={} reason={}", player->GetId(), portalId, reason);
		Metrics::PortalReject.fetch_add(1);
		// 갈 수 없는 포털(이 서버에 없는 레벨)만 사유를 알린다. 재사용 대기를 걸어 응답을 도배에 쓰지 못하게 한다.
		if (unavailable)
		{
			player->portalCooldown.Reset(world->portalCooldownMs);
			Metrics::RoomMoveRejected.fetch_add(1);
			if (shared_ptr<Session> session = player->GetSession())
				session->Send(PacketSerializer::MAKE_SC_MOVE_RESULT(Protocol::MOVE_RESULT_NOT_AVAILABLE));
		}
		return;
	}

	player->portalCooldown.Reset(world->portalCooldownMs);

	// 조준 중이면 해제한다(시간 초과와 같은 알림: 다른 사람 + 본인에게 seq).
	if (player->IsSkillAiming())
	{
		player->SetSkillAim(nullptr, nowMs);
		BroadcastAOI(static_pointer_cast<Character>(player), PacketSerializer::MAKE_SC_SKILL_AIM(player->GetId(), 0, false, 0));
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_SKILL_AIM(player->GetId(), 0, false, player->aim.seq));
	}

	// 다른 레벨로 가는 포털(던전 입구·출구, DG3c): 도착 Room(인스턴스 생성·합류 포함)은 서비스가, 자리는 도착 Room이 정한다.
	if (route == PortalRoute::OtherLevel)
	{
		Metrics::PortalUsed.fetch_add(1);
		LOG_DEBUG("Portal", "player={} portal={} -> 레벨 '{}' 지점 {}", player->GetId(), portalId, portal->TargetMap, portal->TargetPointId);
		MoveRequest request;
		request.kind = MoveRequest::Kind::LevelPortal;
		request.levelName = portal->TargetMap;
		request.pointId = portal->TargetPointId;
		BeginMove(player, Player::MoveReason::Portal, request, nullptr);
		return;
	}

	// 위치: 방향은 그대로, 속도는 0.
	Protocol::PositionInfo newPos = player->GetPosition();
	newPos.set_x(targetPos.x());
	newPos.set_y(targetPos.y());
	newPos.set_z(targetPos.z());
	newPos.set_v_x(0.f);
	newPos.set_v_y(0.f);
	newPos.set_v_z(0.f);

	// 도착지가 다른 구역이면 같은 채널의 그 구역 Room으로 옮긴다(DG3b). 위치는 도착 Room이 넣는다.
	if (route == PortalRoute::OtherRoom)
	{
		Metrics::PortalUsed.fetch_add(1);
		LOG_DEBUG("Portal", "player={} portal={} -> {} (구역 '{}' Room으로 이동)", player->GetId(), portalId, portal->TargetPointId, targetArea);
		MoveRequest request;
		request.area = targetArea;
		BeginMove(player, Player::MoveReason::Portal, request, &newPos);
		return;
	}
	player->SetPosition(newPos);

	// 이동 전에 보낸 이동 패킷이 뒤늦게 와도 버려지도록 보정 순번을 올리고, 이동 예산은 0에서 다시 모은다(부활과 같음).
	Player::MoveGuard& guard = player->moveGuard;
	++guard.correctionSeq;
	guard.budgetCm = 0.f;
	guard.lastAccrueMs = nowMs;

	Metrics::PortalUsed.fetch_add(1);
	LOG_DEBUG("Portal", "player={} portal={} -> {} pos=({:.0f},{:.0f},{:.0f})", player->GetId(), portalId, portal->TargetPointId,
		newPos.x(), newPos.y(), newPos.z());

	// 떠나는 자리에서 보던 사람들에게 먼저 알리고(본인 포함), 그 다음 시야를 새 위치 기준으로 갱신한다.
	// 계속 보이는 사람은 SC_TELEPORT의 위치로 옮기고, 새 자리에서 처음 보게 된 사람은 UpdateView의 SC_ADD_OBJECT로 받는다.
	BroadcastAOI(static_pointer_cast<Character>(player), PacketSerializer::MAKE_SC_TELEPORT(player, guard.correctionSeq), true);

	ViewUpdate result = _gameMap.UpdateMap(player->GetId(), player->GetPosition(), player->_viewList);
	if (!result.entered.empty() || !result.leaved.empty())
		UpdateView(player, result);

	UpdateDropView(player);
	UpdateNpcView(player);

	// 거래 중이면 상대와의 거리를 본다(PlayerMove와 같음): TradeRangeCm을 넘으면 취소. 확정 중이면 DB 결과를 기다린다.
	if (player->tradeId != 0)
	{
		const TradeSession* trade = FindTrade(player);
		if (trade != nullptr && trade->phase != Protocol::TRADE_PHASE_CONFIRMING && !IsTradeRangeOk(*trade))
			EndTrade(trade->id, Protocol::TRADE_RESULT_TOO_FAR);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Room 간 이동 (DG3b, D-216 — DESIGN_PARTY_DUNGEON §4-2)
// 규칙: Player의 필드는 그 플레이어를 가진 Room(_ownerRoom)의 잡 안에서만 만진다. 출발 Room이 _objects·_players에서 뺀 뒤
// _ownerRoom을 도착 Room으로 바꾸고 들어오기 잡을 넣으면, 그 뒤로는 도착 Room만 이 플레이어를 만진다.
// DB 결과는 요청한 Room으로 돌아오므로(DatabaseWorker), 떠나기 전에 DB에 가 있는 작업이 0건이어야 한다(Player::HasUnfinishedWork)
// — 대기·이동 표시 중에는 새 DB 작업을 시작하지 않는다(CheckItemRequest·장착·액티브 칸·거래 신청·주기 저장).
// ---------------------------------------------------------------------------------------------------------------
namespace
{
	Protocol::RoomKind ToProtoKind(RoomKind kind)
	{
		switch (kind)
		{
		case RoomKind::Town: return Protocol::ROOM_KIND_TOWN;
		case RoomKind::Field: return Protocol::ROOM_KIND_FIELD;
		case RoomKind::Dungeon: return Protocol::ROOM_KIND_DUNGEON;
		}
		return Protocol::ROOM_KIND_NONE;
	}
}

Protocol::PositionInfo Room::PickStartPosition()
{
	Protocol::PositionInfo pos;
	const vector<PlayerStartPoint>& starts = _gameMap.GetPlayerStarts();
	if (starts.empty()) return pos;

	// 이 구역에 시작 지점이 없으면(잘못된 배치) 레벨의 첫 지점을 쓴다.
	const PlayerStartPoint& start = _startIndices.empty() ? starts[0] : starts[_startIndices[_nextStart++ % _startIndices.size()]];
	pos.set_x(start.Pos.X);
	pos.set_y(start.Pos.Y);
	pos.set_z(start.Pos.Z);
	pos.set_yaw(start.Pos.Yaw);
	if (_info.area.empty()) return pos;   // 구역이 없는 Room(옛 맵): 마커 자리 그대로

	// 마커 중심에서 반경 3m 안의 걷는 자리. 마커 z는 지면 + 100cm이고 내비메시 보정은 지면 높이를 주므로 같은 만큼 올린다.
	constexpr float START_SCATTER_CM = 300.f;
	constexpr float START_UP_CM = 100.f;
	NavmeshManager* nav = GetNavManager();
	for (int i = 0; i < 8 && nav != nullptr; ++i)
	{
		const float angle = Utils::GetRandom<float>(0.f, 6.2831853f);
		const float dist = START_SCATTER_CM * sqrtf(Utils::GetRandom<float>(0.f, 1.f));
		Protocol::PositionInfo candidate;
		candidate.set_x(start.Pos.X + cosf(angle) * dist);
		candidate.set_y(start.Pos.Y + sinf(angle) * dist);
		candidate.set_z(start.Pos.Z);
		if (!nav->AdjustPositionToNavMesh(candidate)) continue;
		const float dx = candidate.x() - start.Pos.X, dy = candidate.y() - start.Pos.Y;
		if (dx * dx + dy * dy > (START_SCATTER_CM + 100.f) * (START_SCATTER_CM + 100.f)) continue;   // 보정이 멀리 끌고 갔다(장애물 옆)
		pos.set_x(candidate.x());
		pos.set_y(candidate.y());
		pos.set_z(candidate.z() + START_UP_CM);
		break;
	}
	return pos;
}

void Room::PlaceNewPlayer(const shared_ptr<Player>& player)
{
	if (_gameMap.GetPlayerStarts().empty() && !player->isDummy) return;   // 시작 지점이 없는 맵: 지금 위치 그대로(기존 동작)

	Protocol::PositionInfo pos = PickStartPosition();
	if (player->isDummy)
	{
		// 더미 봇([Debug]): DummySpawnZone이 있으면 그 안, 없으면 구역이 있는 마을 Room은 시작 지점(실제 규칙),
		// 그 밖(사냥터 Room·구역이 없는 Room)은 걷는 영역 전체에서 무작위(다른 구역 Room의 땅 = 마을 안은 피한다).
		Protocol::PositionInfo dummyPos;
		if (PickDummyZonePosition(dummyPos))
			pos = dummyPos;
		else if (_info.area.empty() || _info.kind != RoomKind::Town)
		{
			for (int i = 0; i < 20; ++i)
			{
				dummyPos = _gameMap.GetNavManager()->GetRandomPosition();
				if (!_gameMap.IsInOtherAreaZone(dummyPos.x(), dummyPos.y())) break;
			}
			pos = dummyPos;
		}
	}
	player->SetPosition(pos);
	player->spawnPosition = pos;
}

void Room::BeginMove(const shared_ptr<Player>& player, Player::MoveReason reason, const MoveRequest& request, const Protocol::PositionInfo* destPos)
{
	Player::RoomMoveState& move = player->roomMove;
	if (move.Active()) return;

	// 확정 전의 거래는 지금 취소한다 — 대기하는 동안 상대의 확정으로 DB 작업이 새로 시작되지 않게(확정 중이면 그대로 두고 결과를 기다린다).
	CancelTradeOf(player, Protocol::TRADE_RESULT_MOVED);

	move = {};
	move.waiting = true;
	move.reason = reason;
	move.request = request;
	if (destPos != nullptr)
	{
		move.hasDestPos = true;
		move.destPos = *destPos;
	}
	move.deadlineMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count() + GServerConfig.moveWaitTimeoutMs;
	Metrics::RoomMoveStarted.fetch_add(1);
	TryAdvanceMove(player);
}

void Room::TryAdvanceMove(const shared_ptr<Player>& player)
{
	if (player == nullptr || !player->roomMove.waiting) return;
	if (!IsInRoom(player)) return;   // 그 사이 접속 해제로 나갔다(퇴장이 이동 상태를 지웠다)

	// 대기 중에 죽으면 포털 이동은 취소한다(부활·따라가기·내보냄은 죽은 채로 간다).
	if (player->roomMove.reason == Player::MoveReason::Portal && player->GetStat().IsDead())
	{
		CancelMove(player, Protocol::MOVE_RESULT_NONE);
		return;
	}
	if (player->HasUnfinishedWork()) return;   // 아직 DB에 가 있는 작업이 있다 — 결과 처리 끝에서 다시 온다

	// ③ 이동 표시. 이 뒤로 이 플레이어의 요청은 버리고(중복 요청 포함), 새 DB 작업도 시작하지 않는다.
	player->roomMove.waiting = false;
	player->roomMove.transfer = true;
	GWorld->PushJob(&WorldService::RequestMove, player, shared_from_this(), player->channel, player->roomMove.request);
}

void Room::CancelMove(const shared_ptr<Player>& player, Protocol::MoveResult notify)
{
	if (!player->roomMove.Active()) return;
	LOG_DEBUG("RoomMove", "취소 player={} result={}", player->GetId(), Protocol::MoveResult_Name(notify));
	const Player::MoveReason reason = player->roomMove.reason;
	player->roomMove = {};

	if (notify == Protocol::MOVE_RESULT_TIMEOUT) Metrics::RoomMoveTimeout.fetch_add(1);
	else if (notify == Protocol::MOVE_RESULT_NONE) Metrics::RoomMoveCanceled.fetch_add(1);
	else Metrics::RoomMoveRejected.fetch_add(1);   // 갈 수 없음(NOT_AVAILABLE·DUNGEON_FULL·NOT_LEADER·DUNGEON_CLEARED)

	// 파티장을 따라가려던 이동·서버가 내보내려던 이동은 본인이 요청한 것이 아니라 사유를 알리지 않는다.
	if (notify != Protocol::MOVE_RESULT_NONE && reason != Player::MoveReason::Follow && reason != Player::MoveReason::Eject)
	{
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_MOVE_RESULT(notify));
	}

	// 다른 이동의 응답을 기다리느라 미뤄 둔 내보내기(EjectPlayer)를 이어서 한다. 접속이 끊긴 사람은 퇴장 잡이 정리한다.
	if (player->ejectPending)
	{
		player->ejectPending = false;
		const shared_ptr<GameSession> session = player->GetSession();
		if (IsInRoom(player) && session != nullptr && session->IsConnected())
			EjectPlayer(player);
	}
}

void Room::UpdateMoves(long long nowMs)
{
	// CancelMove → EjectPlayer가 이동 상태를 다시 켤 수 있지만 _players 자체는 바뀌지 않는다.
	for (const auto& [id, player] : _players)
	{
		if (!player->roomMove.waiting) continue;
		if (nowMs >= player->roomMove.deadlineMs)
		{
			// 서버가 내보내는 이동은 포기하지 않는다(남으면 닫혀야 할 인스턴스에 사람이 남는다) — 시한만 다시 잡고 계속 기다린다.
			if (player->roomMove.reason == Player::MoveReason::Eject)
			{
				LOG_WARN("RoomMove", "내보내기 대기가 {}ms를 넘겼습니다 (PlayerId {}, itemOp {} progress {} quickSlot {}) — 계속 기다립니다", GServerConfig.moveWaitTimeoutMs,
					player->GetPlayerUID(), player->itemOp.inFlight, player->progressSave.inFlight, player->quickSlotSave.inFlight);
				player->roomMove.deadlineMs = nowMs + GServerConfig.moveWaitTimeoutMs;
				TryAdvanceMove(player);
			}
			else
				CancelMove(player, Protocol::MOVE_RESULT_TIMEOUT);
		}
		else
			TryAdvanceMove(player);
	}
}

void Room::EjectPlayer(shared_ptr<Player> player)
{
	// 방에 없으면(이미 나갔거나, 아직 로딩 대기 중이거나, 접속이 끝남) 할 일이 없다 — 로딩을 마치면 서비스가 다시 본다(WorldService::OnPlayerMoved).
	if (player == nullptr || !IsInRoom(player)) return;

	Player::RoomMoveState& move = player->roomMove;
	if (move.transfer)
	{
		// 다른 이동(출구 포털 등)의 응답을 기다리는 중: 그 이동이 되면 어차피 이 방을 떠나고, 취소되면 CancelMove가 이어서 내보낸다.
		player->ejectPending = true;
		return;
	}
	if (move.waiting && move.reason == Player::MoveReason::Eject) return;   // 이미 내보내는 중

	// 대기 중이던 다른 이동(출구 포털)은 내보내기로 바꾼다.
	if (move.waiting)
	{
		Metrics::RoomMoveCanceled.fetch_add(1);
		move = {};
	}
	MoveRequest request;
	request.area = AREA_TOWN;
	LOG_DEBUG("RoomMove", "내보내기 player={} Room {} -> 마을", player->GetId(), _info.roomId);
	BeginMove(player, Player::MoveReason::Eject, request, nullptr);
}

void Room::EjectAll()
{
	// BeginMove는 _players를 바꾸지 않는다(떠나기는 서비스의 응답 잡에서).
	for (const auto& [id, player] : _players)
		EjectPlayer(player);
}

void Room::OnMoveResolved(shared_ptr<Player> player, MoveGrant grant)
{
	// 가지 못하게 되면 서비스가 잡아 둔 인스턴스 자리를 돌려준다(자리가 남으면 그 인스턴스가 닫히지 않는다).
	auto releaseSeat = [&]() {
		if (grant.instanceId != 0 && player != nullptr)
			GWorld->PushJob(&WorldService::OnMoveAborted, player->GetId(), grant.instanceId);
	};

	if (player == nullptr || !IsInRoom(player)) { releaseSeat(); return; }   // 그 사이 접속 해제로 나갔다
	if (!player->roomMove.transfer) { releaseSeat(); return; }

	const shared_ptr<GameSession> session = player->GetSession();
	if (session == nullptr || !session->IsConnected())
	{
		// 퇴장 잡이 뒤따라온다 — 여기서는 이동만 취소한다.
		releaseSeat();
		CancelMove(player, Protocol::MOVE_RESULT_NONE);
		return;
	}
	if (grant.dest == nullptr || grant.dest.get() == this)
	{
		releaseSeat();
		CancelMove(player, grant.dest == nullptr ? grant.result : Protocol::MOVE_RESULT_NOT_AVAILABLE);
		return;
	}
	if (player->roomMove.reason == Player::MoveReason::Portal && player->GetStat().IsDead())
	{
		releaseSeat();
		CancelMove(player, Protocol::MOVE_RESULT_NONE);
		return;
	}
	if (player->HasUnfinishedWork())
	{
		// 이동 표시 뒤에는 새 DB 작업이 시작되지 않아야 한다. 왔다면 막지 못한 경로가 있는 것 — 결과가 옛 Room으로 돌아오지 않게 이동을 취소한다.
		LOG_ERROR("RoomMove", "떠나기 직전에 끝나지 않은 DB 작업이 있습니다 (PlayerId {}, itemOp {} progress {} quickSlot {}) — 이동을 취소합니다",
			player->GetPlayerUID(), player->itemOp.inFlight, player->progressSave.inFlight, player->quickSlotSave.inFlight);
		Metrics::RoomMoveAbortBusy.fetch_add(1);
		releaseSeat();
		CancelMove(player, Protocol::MOVE_RESULT_TIMEOUT);
		return;
	}

	// ⑤ 떠나기(PlayerLeaveRoom에서 접속 해제 전용인 저장 요청·정리 신호를 뺀 것). 이동 상태(roomMove)는 도착 Room이 읽고 지운다.
	const int playerId = player->GetId();
	ViewUpdate result = _gameMap.LeaveMap(playerId, player->_viewList);
	if (!result.leaved.empty())
		UpdateView(player, result);
	player->_viewList.clear();

	if (false == RemoveObject(playerId))
		LOG_ERROR("RoomMove", "방에 없는 플레이어 id {}", playerId);
	if (_players.erase(playerId) > 0)
		_playerCount.fetch_sub(1);

	CancelTradeOf(player, Protocol::TRADE_RESULT_MOVED);   // BeginMove에서 이미 끝냈다(남아 있을 수 없지만 안전하게)
	player->dropView.clear();
	player->npcView.clear();
	if (player->IsSkillAiming())
		player->SetSkillAim(nullptr, chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count());
	player->ejectPending = false;   // 이 방을 떠난다 — 미뤄 둔 내보내기는 필요 없다
	// 레벨이 바뀌면 도착 Room이 클라이언트의 로딩을 기다린다(DG3c).
	player->roomMove.crossLevel = grant.dest->GetInfo().levelName != _info.levelName;

	LOG_DEBUG("RoomMove", "player={} Room {} -> Room {}", playerId, _info.roomId, grant.dest->GetRoomId());

	// 소유 Room을 먼저 바꾸고 들어오기 잡을 넣는다(WorldService::EnterWorld와 같은 순서) — 이 뒤에 끊긴 세션의 퇴장 잡은 도착 Room으로 간다.
	// 이 줄 뒤로 이 Room은 이 플레이어의 필드를 만지지 않는다.
	player->SetOwnerRoom(grant.dest);
	if (GServerConfig.moveHandoffDelayMs > 0)
		grant.dest->ReserveJobAlways(static_cast<DWORD>(GServerConfig.moveHandoffDelayMs), &Room::PlayerArriveRoom, player);
	else
		grant.dest->PushJob(&Room::PlayerArriveRoom, player);

	// 파티장의 던전 입장: 같은 방의 파티원도 그 인스턴스로 보낸다(D-218). 멀리 있어도, 전투·거래 중이어도, 죽어 있어도 간다.
	// 이미 다른 이동 중인 사람과 끝나지 않은 DB 작업이 시한(MoveWaitTimeoutMs)을 넘긴 사람은 남는다(나중에 입구에서 합류).
	for (const int followerId : grant.followers)
	{
		const shared_ptr<Player> follower = Id2Player(followerId);
		if (follower == nullptr || follower->roomMove.Active()) continue;
		const shared_ptr<GameSession> followerSession = follower->GetSession();
		if (followerSession == nullptr || !followerSession->IsConnected()) continue;

		MoveRequest request;
		request.kind = MoveRequest::Kind::Instance;
		request.instanceId = grant.instanceId;
		BeginMove(follower, Player::MoveReason::Follow, request, nullptr);
	}
}

Protocol::PositionInfo Room::PickPositionNearPortal(const PortalTriggerPoint& portal)
{
	Protocol::PositionInfo pos;
	pos.set_x(portal.Pos.X);
	pos.set_y(portal.Pos.Y);
	pos.set_z(portal.Pos.Z);

	// 반경 바로 밖의 고리(반경 + 50 ~ 250cm) 안에서 걷는 자리. 마커 z는 지면 + 100cm이고 내비메시 보정은 지면 높이를 주므로 같은 만큼 올린다.
	constexpr float RING_MIN_CM = 50.f, RING_MAX_CM = 250.f, UP_CM = 100.f;
	NavmeshManager* nav = GetNavManager();
	for (int i = 0; i < 12 && nav != nullptr; ++i)
	{
		const float angle = Utils::GetRandom<float>(0.f, 6.2831853f);
		const float dist = portal.TriggerRadius + Utils::GetRandom<float>(RING_MIN_CM, RING_MAX_CM);
		Protocol::PositionInfo candidate;
		candidate.set_x(portal.Pos.X + cosf(angle) * dist);
		candidate.set_y(portal.Pos.Y + sinf(angle) * dist);
		candidate.set_z(portal.Pos.Z);
		if (!nav->AdjustPositionToNavMesh(candidate)) continue;
		const float dx = candidate.x() - portal.Pos.X, dy = candidate.y() - portal.Pos.Y;
		const float d = sqrtf(dx * dx + dy * dy);
		if (d > portal.TriggerRadius + RING_MAX_CM + 100.f) continue;   // 보정이 멀리 끌고 갔다
		if (_gameMap.IsInOtherAreaZone(candidate.x(), candidate.y())) continue;   // 다른 구역 Room의 땅
		pos.set_x(candidate.x());
		pos.set_y(candidate.y());
		pos.set_z(candidate.z() + UP_CM);
		break;
	}
	return pos;
}

void Room::PlayerArriveRoom(shared_ptr<Player> player)
{
	if (player == nullptr) return;
	if (player->GetCurrentRoom().get() != this)
	{
		LOG_ERROR("RoomMove", "들어오기: 플레이어 {} 의 소유 Room이 이 Room({})이 아닙니다", player->GetId(), _info.roomId);
		Metrics::OwnerViolation.fetch_add(1);
		return;
	}

	const Player::RoomMoveState move = player->roomMove;
	player->roomMove = {};

	// 떠난 뒤 이 잡 사이에 끊겼으면 들이지 않는다. 퇴장 잡이 먼저 왔든 뒤에 오든 정리 신호는 이 Room에서 한 번 나간다.
	// 떠난 Room은 저장을 요청하지 않았으므로 여기서 마지막 진행 상황을 저장한다(값이 DB와 다를 때만 보낸다).
	const shared_ptr<GameSession> session = player->GetSession();
	if (session == nullptr || !session->IsConnected())
	{
		LOG_DEBUG("RoomMove", "Player[{}] 들어오기 취소: 세션 끊김", player->GetId());
		Metrics::RoomMoveCanceled.fetch_add(1);
		RequestProgressSave(player);
		RequestQuickSlotSave(player);
		NotifyClosedIfDone(player);
		return;
	}
	if (_closed.load())
	{
		// 서비스는 들어오는 사람이 있는 인스턴스를 닫지 않는다. 왔다면 자리 관리에 구멍이 있는 것 — 어느 방에도 없는 채 남지 않게 끊는다(퇴장 잡이 정리한다).
		LOG_ERROR("RoomMove", "닫힌 Room {} 에 플레이어 {} 가 도착했습니다 — 접속을 끊습니다", _info.roomId, player->GetId());
		session->ForceClose(L"RoomMove: arrived at a closed room");
		return;
	}

	const long long nowMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();

	// 위치: 같은 레벨 포털은 도착 마커(방향 유지), 다른 레벨의 포털 마커를 가리키면 그 마커 바로 밖(던전 출구 → 입구 앞),
	// 그 밖(부활·내보냄·던전 입장)은 이 Room의 시작 지점 가운데 하나. 속도는 0.
	Protocol::PositionInfo pos;
	const PortalTriggerPoint* arrivePortal = nullptr;
	if (move.hasDestPos)
		pos = move.destPos;
	else if (move.request.kind == MoveRequest::Kind::LevelPortal && move.request.pointId != 0
		&& (arrivePortal = _gameMap.FindPortal(move.request.pointId)) != nullptr)
		pos = PickPositionNearPortal(*arrivePortal);
	else
		pos = PickStartPosition();
	pos.set_v_x(0.f);
	pos.set_v_y(0.f);
	pos.set_v_z(0.f);
	player->SetPosition(pos);
	player->spawnPosition = pos;

	if (move.reason == Player::MoveReason::Revive)
	{
		player->GetStat().Revive(_world->reviveHpPercent);
		player->ClearActionLocks();
		player->deadAtMs = 0;
		Metrics::Revives.fetch_add(1);
	}
	// 시전 중에 끌려온 사람(파티장 따라가기·내보냄)의 행동 잠금을 푼다(D-225). 죽은 채 온 사람은 여기서 부활 요청을 보낸다.
	else if (move.reason == Player::MoveReason::Follow || move.reason == Player::MoveReason::Eject)
		player->ClearActionLocks();

	// 이동 전에 보낸 이동 패킷이 뒤늦게 와도 버려지도록 보정 순번을 올린다(0으로 되돌리지 않는다 — 클라이언트의 순번과 이어져야 한다). 이동 예산은 0에서 다시.
	// 스킬 쿨타임·포털 재사용 대기·아이템 재사용 시간·주기 저장 시각은 그대로 이어진다.
	Player::MoveGuard& guard = player->moveGuard;
	++guard.correctionSeq;
	guard.budgetCm = 0.f;
	guard.lastAccrueMs = nowMs;

	session->Send(PacketSerializer::MAKE_SC_ROOM_CHANGE(player, _info.levelName, ToProtoKind(_info.kind), _info.channel, guard.correctionSeq));

	// 레벨이 바뀌면 클라이언트가 그 레벨을 열고 CS_ENTER_ROOM을 보낼 때까지 기다린다 — 그동안 시야·전투에 넣지 않는다(DG3c).
	if (move.crossLevel)
	{
		LoadingEntry entry;
		entry.player = player;
		entry.reason = move.reason;
		entry.deadlineMs = nowMs + static_cast<long long>(GServerConfig.loadTimeoutSec) * 1000;
		_loading[player->GetId()] = std::move(entry);
		LOG_DEBUG("RoomMove", "Player[{}] 로딩 대기: Room {} 레벨 '{}'", player->GetId(), _info.roomId, _info.levelName);
		return;
	}
	FinishArrive(player, move.reason);
}

void Room::PlayerLoadDone(shared_ptr<Player> player)
{
	if (player == nullptr) return;
	const auto it = _loading.find(player->GetId());
	if (it == _loading.end() || it->second.player != player) return;   // 로딩 대기 중이 아니다(처음 입장의 중복 요청·같은 레벨 이동)

	const Player::MoveReason reason = it->second.reason;
	_loading.erase(it);

	const shared_ptr<GameSession> session = player->GetSession();
	if (session == nullptr || !session->IsConnected())
	{
		Metrics::RoomMoveCanceled.fetch_add(1);
		RequestProgressSave(player);
		RequestQuickSlotSave(player);
		NotifyClosedIfDone(player);
		return;
	}

	// 로딩하는 동안의 시간은 이동 예산에 넣지 않는다.
	player->moveGuard.budgetCm = 0.f;
	player->moveGuard.lastAccrueMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	FinishArrive(player, reason);
}

void Room::UpdateLoading(long long nowMs)
{
	for (const auto& [id, entry] : _loading)
	{
		if (nowMs < entry.deadlineMs) continue;
		// 끊으면 퇴장 잡이 이 Room으로 와서 목록에서 빼고 정리한다(PlayerLeaveRoom). ForceClose는 여러 번 불러도 한 번만 동작한다.
		if (shared_ptr<GameSession> session = entry.player->GetSession())
		{
			LOG_WARN("RoomMove", "로딩 대기 {}초를 넘겼습니다 (PlayerId {}, Room {}) — 접속을 끊습니다", GServerConfig.loadTimeoutSec, entry.player->GetPlayerUID(), _info.roomId);
			session->ForceClose(L"RoomMove: load timeout");
		}
	}
}

void Room::FinishArrive(const shared_ptr<Player>& player, Player::MoveReason reason)
{
	const shared_ptr<GameSession> session = player->GetSession();
	if (session == nullptr) return;

	if (false == AddObject(player))
	{
		// 어느 방에도 없는 채 접속만 남지 않게 끊는다 — 퇴장 잡이 이 Room에서 정리 신호를 보낸다.
		session->ForceClose(L"RoomMove: AddObject failed");
		return;
	}
	if (_players.emplace(player->GetId(), player).second)
		_playerCount.fetch_add(1);

	session->Send(PacketSerializer::MAKE_SC_ADD_OBJECT(player));
	if (_portalListBuffer != nullptr)
		session->Send(_portalListBuffer);
	// 부활 이동: 본인에게만 알린다(이 Room의 다른 사람은 아래 SC_ADD_OBJECT로 살아 있는 모습을 처음 본다).
	if (reason == Player::MoveReason::Revive)
		session->Send(PacketSerializer::MAKE_SC_REVIVE(player, player->moveGuard.correctionSeq));
	if (_info.kind == RoomKind::Dungeon)
		session->Send(MakeDungeonStatePacket());

	ViewUpdate result = _gameMap.EnterMap(player->GetId(), player->GetPosition());
	if (!result.entered.empty())
		UpdateView(player, result);

	player->dropView.clear();
	UpdateDropView(player);
	player->npcView.clear();
	UpdateNpcView(player);

	Metrics::RoomMoveDone.fetch_add(1);
	LOG_DEBUG("RoomMove", "Player[{}] 들어옴: Room {} pos=({:.0f},{:.0f},{:.0f}) seq={}", player->GetId(), _info.roomId,
		player->GetPosition().x(), player->GetPosition().y(), player->GetPosition().z(), player->moveGuard.correctionSeq);

	// 대기·이동 중에 미뤄 둔 액티브 칸 저장(값이 DB와 다를 때만 보낸다). 진행 상황은 주기 저장이 이어받는다.
	RequestQuickSlotSave(player);

	// ⑧ 명부의 "지금 있는 Room" 갱신. 던전이면 서비스가 이 사람이 아직 들어올 자격이 있는지도 본다(그 사이 파티 탈퇴 → 내보냄).
	GWorld->PushJob(&WorldService::OnPlayerMoved, player->GetId(), shared_from_this());
}

shared_ptr<SendBuffer> Room::MakeDungeonStatePacket() const
{
	if (!_cleared)
		return PacketSerializer::MAKE_SC_DUNGEON_STATE(Protocol::DUNGEON_STATE_IN_PROGRESS, 0);
	const long long nowMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	const long long remainMs = static_cast<long long>(_world->dungeonClearExitSec) * 1000 - (nowMs - _clearedAtMs);
	return PacketSerializer::MAKE_SC_DUNGEON_STATE(Protocol::DUNGEON_STATE_CLEARED, static_cast<int>(max<long long>(0, (remainMs + 999) / 1000)));
}

void Room::OnBossDead()
{
	if (_info.kind != RoomKind::Dungeon || _cleared) return;
	if (--_bossAlive > 0) return;

	// 클리어(D-218): 안에 있는 전원에게 알리고, 서비스가 DungeonClearExitSec 뒤 남은 사람을 마을로 내보낸다. 새로 들어올 수는 없다.
	_cleared = true;
	_clearedAtMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	LOG_INFO("Room", "Room {} 던전 클리어 (안에 {}명)", _info.roomId, _players.size());
	Broadcast(MakeDungeonStatePacket());
	GWorld->PushJob(&WorldService::OnDungeonCleared, _info.roomId);
}

void Room::CheckDbResultOwner(const shared_ptr<Player>& player, const char* what)
{
	if (player == nullptr || IsInRoom(player)) return;
	const shared_ptr<Room> owner = player->GetCurrentRoom();
	if (owner == nullptr || owner.get() == this) return;   // 접속 해제로 이 방에서 나간 플레이어(정상 — 반영을 건너뛴다)

	// 다른 Room으로 옮긴 플레이어의 DB 결과가 옛 Room으로 왔다: 메모리에 반영되지 않으므로 접속을 끊어 다음 로그인 때 DB 값을 읽게 한다.
	LOG_ERROR("RoomMove", "DB 결과({})가 옛 Room {} 으로 왔습니다 (PlayerId {}, 지금 Room {}) — 접속을 끊습니다", what, _info.roomId, player->GetPlayerUID(), owner->GetRoomId());
	Metrics::DbResultOrphan.fetch_add(1);
	if (shared_ptr<GameSession> session = player->GetSession())
		session->ForceClose(L"RoomMove: DB result arrived at the old room");
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

void Room::PlayerChangeEquip(shared_ptr<Player> player, Protocol::EquipPart part, int64_t itemDbId)
{
	if (player == nullptr) return;

	int itemId = 0;   // 요청한 행의 종류(찾은 뒤에 채운다)
	auto reject = [&](Protocol::EquipResult result) {
		LOG_DEBUG("Equip", "거부 player={} part={} dbId={} item={} result={}", player->GetId(), static_cast<int>(part), itemDbId, itemId, Protocol::EquipResult_Name(result));
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_CHANGE_EQUIP_RESULT(result, part, itemId, itemDbId));
	};

	if (false == IsInRoom(player)) { reject(Protocol::EQUIP_RESULT_NOT_IN_ROOM); return; }
	// 더미 봇은 전원이 DB 한 행을 써서 item_db_id 기준 장착을 할 수 없다(D-161).
	if (player->isDummy || player->GetPlayerUID() == 0) { reject(Protocol::EQUIP_RESULT_NOT_ALLOWED); return; }

	// 검증 순서: 형식(슬롯·보유·아이템 슬롯) → 상태(사망·시전·레벨) → 빈도 → 진행 중인 작업 → 빈 칸.
	// 빈도 제한은 통과한 요청만 시간을 갱신한다.
	if (part < Protocol::EQUIP_PART_HEAD || part > Protocol::EQUIP_PART_OFFHAND) { reject(Protocol::EQUIP_RESULT_WRONG_SLOT); return; }

	const ItemInstance* newItem = nullptr;
	const ItemData* item = nullptr;
	bool alreadyEquipped = false;   // 요청한 행이 이미 그 슬롯에 있다(바뀌는 것이 없음)
	if (itemDbId != 0)
	{
		newItem = player->inventory.Find(itemDbId);
		if (newItem == nullptr) { reject(Protocol::EQUIP_RESULT_NOT_OWNED); return; }   // 없는 행, 다른 사람의 행
		itemId = newItem->itemId;
		item = DataManager::GetItemData(itemId);   // 로그인 때 ItemTable에 있는 것만 올렸다
		if (item == nullptr) { reject(Protocol::EQUIP_RESULT_NO_ITEM); return; }
		if (static_cast<int>(item->slot) != static_cast<int>(part)) { reject(Protocol::EQUIP_RESULT_WRONG_SLOT); return; }
		alreadyEquipped = newItem->equipPart == static_cast<int>(part);
		if (!alreadyEquipped && newItem->equipPart != 0) { reject(Protocol::EQUIP_RESULT_NOT_OWNED); return; }   // 인벤토리 위치의 행만 장착한다
		if (IsItemInTrade(player, itemDbId)) { reject(Protocol::EQUIP_RESULT_IN_TRADE); return; }   // 거래에 올린 행 (A4.1)
	}
	// 보조손(방패)은 한손검을 든 상태에서만 착용한다 (MC4.2f, DECISIONS 2026-11-09). 해제는 언제나 허용.
	if (part == Protocol::EQUIP_PART_OFFHAND && itemDbId != 0 && player->GetWeaponType() != WeaponType::Sword) { reject(Protocol::EQUIP_RESULT_NEED_SWORD); return; }
	if (player->GetStat().IsDead()) { reject(Protocol::EQUIP_RESULT_DEAD); return; }
	if (player->IsCasting()) { reject(Protocol::EQUIP_RESULT_ACTING); return; }
	if (item != nullptr && item->requiredLevel > player->GetInfo().level()) { reject(Protocol::EQUIP_RESULT_LEVEL_TOO_LOW); return; }

	const MovementData* rule = DataManager::GetMovementData(PLAYER_MOVEMENT_ID);   // 서버 시작 때 존재를 확인했다
	if (!player->equipCooldown.IsReady()) { reject(Protocol::EQUIP_RESULT_TOO_FREQUENT); return; }
	player->equipCooldown.Reset(rule->equipMinIntervalMs);

	// 바뀌는 것이 없으면(이미 장착한 행, 빈 슬롯 해제) 조용히 끝낸다 (잠금·방송·DB 없음)
	const int64_t oldDbId = player->inventory.GetEquippedDbId(part);
	if (alreadyEquipped || (itemDbId == 0 && oldDbId == 0)) return;

	if (player->itemOp.inFlight || player->roomMove.Active()) { Metrics::ItemOpBusy.fetch_add(1); reject(Protocol::EQUIP_RESULT_BUSY); return; }

	// 무기가 한손검이 아니게 되면(활·맨손 포함) 보조손(방패)을 함께 내린다 (MC4.2f)
	int64_t offhandDbId = 0;
	if (part == Protocol::EQUIP_PART_WEAPON)
	{
		const WeaponType newWeaponType = item ? item->weaponType : WeaponType::Unarmed;
		if (newWeaponType != WeaponType::Sword)
			offhandDbId = player->inventory.GetEquippedDbId(Protocol::EQUIP_PART_OFFHAND);
	}

	// 인벤토리 칸(A6.1): 내려오는 행(옛 장비·방패)이 들어갈 칸을 정한다. 올라가는 행이 비우는 칸을 먼저 쓰고(옛 장비와 자리 맞바꿈),
	// 모자라면 앞의 빈 칸을 쓴다. 그래서 같은 슬롯 교체는 가득 차 있어도 되고, 칸이 더 필요한 요청만 빈 칸이 없으면 거부한다.
	vector<int> landing;
	if (newItem != nullptr) landing.push_back(newItem->slot);
	const int needSlots = (oldDbId != 0 ? 1 : 0) + (offhandDbId != 0 ? 1 : 0);
	if (static_cast<int>(landing.size()) < needSlots)
	{
		const vector<int> freeSlots = player->inventory.FreeSlots(DataManager::GetEconomy().inventorySlots, needSlots - static_cast<int>(landing.size()));
		landing.insert(landing.end(), freeSlots.begin(), freeSlots.end());
	}
	if (static_cast<int>(landing.size()) < needSlots) { reject(Protocol::EQUIP_RESULT_INVENTORY_FULL); return; }

	DB_ItemOp op;
	op.kind = ItemOpKind::Equip;
	op.itemId = itemId;
	op.equipPart = static_cast<int32_t>(part);
	op.newDbId = itemDbId;
	op.oldDbId = oldDbId;
	op.offhandDbId = offhandDbId;
	op.newSlot = newItem != nullptr ? newItem->slot : -1;
	size_t next = 0;
	if (oldDbId != 0) op.oldSlot = landing[next++];
	if (offhandDbId != 0) op.offhandSlot = landing[next++];
	StartItemOp(player, op);
}

void Room::PlayerDevGive(shared_ptr<Player> player, int itemId, int64_t count)
{
	if (player == nullptr) return;

	auto reject = [&](Protocol::ItemResult result) {
		LOG_DEBUG("Item", "지급 거부 player={} item={} count={} result={}", player->GetId(), itemId, count, Protocol::ItemResult_Name(result));
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_DEV_GIVE, result));
	};

	if (false == GServerConfig.devCommands) { reject(Protocol::ITEM_RESULT_NOT_ALLOWED); return; }
	const Protocol::ItemResult check = CheckItemRequest(player);
	if (check != Protocol::ITEM_RESULT_SUCCESS) { reject(check); return; }

	const EconomyData& economy = DataManager::GetEconomy();
	DB_ItemOp op;
	op.kind = ItemOpKind::Give;
	if (itemId == 0)
	{
		// 골드: 상한을 넘기는 지급은 거부한다. 실제 증액은 DB에서 조건부로 하고 잔액을 돌려받는다(D-152).
		if (count < 1) { reject(Protocol::ITEM_RESULT_NO_ITEM); return; }
		if (count > static_cast<int64_t>(economy.goldMax) - player->GetGold()) { reject(Protocol::ITEM_RESULT_GOLD_FULL); return; }
		op.goldDelta = count;
		op.goldMax = economy.goldMax;
	}
	else
	{
		const ItemData* item = DataManager::GetItemData(itemId);
		if (item == nullptr || count < 1) { reject(Protocol::ITEM_RESULT_NO_ITEM); return; }
		// 인벤토리 전체보다 많은 수량은 계획을 세우기 전에 거부한다(큰 값의 계산 넘침 방어).
		if (count > static_cast<int64_t>(item->maxStack) * economy.inventorySlots) { reject(Protocol::ITEM_RESULT_INVENTORY_FULL); return; }
		// 덜 찬 스택부터 채우고 남으면 새 칸. 전부 들어갈 자리가 없으면 통째로 거부한다(D-155).
		if (false == player->inventory.PlanAdd(itemId, count, item->maxStack, economy.inventorySlots, op.fills, op.newCounts, op.newSlots)) { reject(Protocol::ITEM_RESULT_INVENTORY_FULL); return; }
		op.itemId = itemId;
	}

	player->itemOp.interval.Reset(economy.itemMinIntervalMs);
	StartItemOp(player, op);
}

void Room::PlayerMoveItem(shared_ptr<Player> player, int64_t itemDbId, int toSlot)
{
	if (player == nullptr) return;

	auto reject = [&](Protocol::ItemResult result) {
		LOG_DEBUG("Item", "칸 옮기기 거부 player={} dbId={} to={} result={}", player->GetId(), itemDbId, toSlot, Protocol::ItemResult_Name(result));
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_MOVE_ITEM, result));
	};

	const Protocol::ItemResult check = CheckItemRequest(player);
	if (check != Protocol::ITEM_RESULT_SUCCESS) { reject(check); return; }

	const EconomyData& economy = DataManager::GetEconomy();
	// 인벤토리 위치의 내 행만 옮긴다(장착 중인 행은 장착 해제로 내린다).
	const ItemInstance* item = player->inventory.Find(itemDbId);
	if (item == nullptr || item->equipPart != 0) { reject(Protocol::ITEM_RESULT_NO_ITEM); return; }
	// 대상은 0 ~ 칸 수 - 1. 메모리에 올리지 않은 DB 행이 차지한 칸으로는 옮기지 못한다.
	if (toSlot < 0 || toSlot >= economy.inventorySlots || player->inventory.IsSlotReserved(toSlot)) { reject(Protocol::ITEM_RESULT_BAD_SLOT); return; }
	if (item->slot == toSlot) return;   // 바뀌는 것이 없다

	const ItemInstance* other = player->inventory.FindBySlot(toSlot);

	DB_ItemOp op;
	op.kind = ItemOpKind::Move;
	op.moveDbId = itemDbId;
	op.moveFrom = item->slot;
	op.moveTo = toSlot;
	op.moveOtherDbId = other != nullptr ? other->dbId : 0;

	player->itemOp.interval.Reset(economy.itemMinIntervalMs);
	StartItemOp(player, op);
}

void Room::PlayerPickupItem(shared_ptr<Player> player, int dropId)
{
	if (player == nullptr) return;

	auto reject = [&](Protocol::ItemResult result) {
		LOG_DEBUG("Item", "줍기 거부 player={} drop={} result={}", player->GetId(), dropId, Protocol::ItemResult_Name(result));
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_PICKUP_ITEM, result));
	};

	// 검증 순서: 방 안 → 더미 봇 아님 → 생존 → 요청 간격 → 진행 중인 작업 → 드랍 존재 → 줍는 중 아님 → 거리 → 소유권 → 칸·골드 상한
	if (false == IsInRoom(player)) { reject(Protocol::ITEM_RESULT_NOT_IN_ROOM); return; }
	if (player->isDummy || player->GetPlayerUID() == 0) { reject(Protocol::ITEM_RESULT_NOT_ALLOWED); return; }
	if (player->GetStat().IsDead()) { reject(Protocol::ITEM_RESULT_DEAD); return; }
	const Protocol::ItemResult check = CheckItemRequest(player);
	if (check != Protocol::ITEM_RESULT_SUCCESS) { reject(check); return; }

	const shared_ptr<DropItem> drop = GetDrop(dropId);
	if (drop == nullptr) { reject(Protocol::ITEM_RESULT_NO_ITEM); return; }   // 없는 id, 이미 주웠거나 사라짐
	// 다른 사람의 줍기가 DB에 가 있다. 그쪽이 실패하면 다시 주울 수 있다.
	if (drop->picking) { reject(Protocol::ITEM_RESULT_BUSY); return; }

	const EconomyData& economy = DataManager::GetEconomy();
	const Protocol::PositionInfo& myPos = player->GetPosition();
	const Protocol::PositionInfo& dropPos = drop->GetPosition();
	const float dx = dropPos.x() - myPos.x(), dy = dropPos.y() - myPos.y();
	const float range = static_cast<float>(economy.pickupRangeCm);
	if (dx * dx + dy * dy > range * range) { reject(Protocol::ITEM_RESULT_TOO_FAR); return; }

	// 소유권: 만료는 타이머가 아니라 지금 시각과 비교한다(예약은 취소할 수 없다).
	const long long nowMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	if (!drop->CanPickup(player->GetPlayerUID(), player->partyId, nowMs)) { reject(Protocol::ITEM_RESULT_NOT_OWNER); return; }

	DB_ItemOp op;
	op.kind = ItemOpKind::Pickup;
	op.dropId = dropId;
	const int64_t count = drop->GetCount();
	if (drop->IsGold())
	{
		if (count < 1) { reject(Protocol::ITEM_RESULT_NO_ITEM); return; }
		if (count > static_cast<int64_t>(economy.goldMax) - player->GetGold()) { reject(Protocol::ITEM_RESULT_GOLD_FULL); return; }
		op.goldDelta = count;
		op.goldMax = economy.goldMax;
	}
	else
	{
		const ItemData* item = DataManager::GetItemData(drop->GetItemId());
		if (item == nullptr || count < 1) { reject(Protocol::ITEM_RESULT_NO_ITEM); return; }
		// 덜 찬 스택부터 채우고 남으면 새 칸. 전부 들어갈 자리가 없으면 통째로 거부한다(D-155). 수량 상한도 PlanAdd가 본다.
		if (false == player->inventory.PlanAdd(drop->GetItemId(), count, item->maxStack, economy.inventorySlots, op.fills, op.newCounts, op.newSlots)) { reject(Protocol::ITEM_RESULT_INVENTORY_FULL); return; }
		op.itemId = drop->GetItemId();
	}

	// 줍는 중 표시: DB 결과가 올 때까지 다른 줍기(BUSY)·소멸·상한 제거가 이 드랍을 건드리지 않는다.
	drop->picking = true;
	player->itemOp.interval.Reset(economy.itemMinIntervalMs);
	StartItemOp(player, op);
}

void Room::PlayerDropItem(shared_ptr<Player> player, int64_t itemDbId)
{
	if (player == nullptr) return;

	auto reject = [&](Protocol::ItemResult result) {
		LOG_DEBUG("Item", "버리기 거부 player={} dbId={} result={}", player->GetId(), itemDbId, Protocol::ItemResult_Name(result));
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_DROP_ITEM, result));
	};

	if (false == IsInRoom(player)) { reject(Protocol::ITEM_RESULT_NOT_IN_ROOM); return; }
	if (player->isDummy || player->GetPlayerUID() == 0) { reject(Protocol::ITEM_RESULT_NOT_ALLOWED); return; }
	if (player->GetStat().IsDead()) { reject(Protocol::ITEM_RESULT_DEAD); return; }
	const Protocol::ItemResult check = CheckItemRequest(player);
	if (check != Protocol::ITEM_RESULT_SUCCESS) { reject(check); return; }

	// 인벤토리 위치의 내 행만 버린다(장착 중인 것은 먼저 해제해야 한다). 스택은 통째로 버린다(D-155).
	const ItemInstance* item = player->inventory.Find(itemDbId);
	if (item == nullptr || item->equipPart != 0) { reject(Protocol::ITEM_RESULT_NO_ITEM); return; }
	// 거래에 올린 행은 거래가 끝날 때까지 버릴 수 없다(일부만 올렸어도).
	if (IsItemInTrade(player, itemDbId)) { reject(Protocol::ITEM_RESULT_IN_TRADE); return; }

	const Protocol::PositionInfo& pos = player->GetPosition();
	DB_ItemOp op;
	op.kind = ItemOpKind::Discard;
	op.itemId = item->itemId;
	op.discardDbId = itemDbId;
	op.discardSlot = item->slot;
	op.discardCount = item->count;
	op.discardX = pos.x();
	op.discardY = pos.y();
	op.discardZ = pos.z();

	player->itemOp.interval.Reset(DataManager::GetEconomy().itemMinIntervalMs);
	StartItemOp(player, op);
}

// ---------------------------------------------------------------------------------------------------------------
// 마을 NPC·상점 (L4, D-201·D-202 — DESIGN_ITEMS_TRADE "상점 NPC")
// ---------------------------------------------------------------------------------------------------------------
void Room::SpawnTownNpcs()
{
	int nextId = NPC_ID_BEGIN;
	for (const NpcSpawnPoint& spawn : _gameMap.GetNpcSpawns())
	{
		if (!_gameMap.InArea(spawn.Area)) continue;   // 다른 구역 Room의 NPC(DG3b)
		const NpcData* data = DataManager::GetNpcData(spawn.NpcTemplateId);
		if (data == nullptr)
		{
			// 방은 실행 중에도 만들어지므로 서버를 멈추지 않고 ERROR만 남긴다(몬스터 스폰과 같은 규칙).
			LOG_ERROR("Room", "Npc 마커 {} 의 npcTemplateId {} 가 NpcTable에 없어 놓지 않습니다", spawn.PointId, spawn.NpcTemplateId);
			continue;
		}
		// 마커는 지면보다 높게 놓이므로 걷는 영역(지면 높이) 위로 맞춘다. 걷는 영역 밖이면 마커 위치를 그대로 쓰고 경고한다(플레이어가 다가갈 수 없을 수 있음).
		MapPosition pos = spawn.Pos;
		Protocol::PositionInfo adjusted;
		adjusted.set_x(pos.X);
		adjusted.set_y(pos.Y);
		adjusted.set_z(pos.Z);
		NavmeshManager* nav = GetNavManager();
		if (nav != nullptr && nav->AdjustPositionToNavMesh(adjusted))
		{
			pos.X = adjusted.x();
			pos.Y = adjusted.y();
			pos.Z = adjusted.z();
		}
		else
		{
			LOG_WARN("Room", "Npc 마커 {} 가 걷는 영역 위가 아닙니다 ({:.0f}, {:.0f}, {:.0f})", spawn.PointId, pos.X, pos.Y, pos.Z);
		}
		const shared_ptr<TownNpc> npc = make_shared<TownNpc>(nextId++, *data, pos);
		LOG_INFO("Room", "마을 NPC 배치: id {} 종류 {} 상점 {} 위치 ({:.0f}, {:.0f}, {:.0f})", npc->GetId(), data->templateId, data->shopId, pos.X, pos.Y, pos.Z);
		npc->SetOwnerRoom(shared_from_this());
		_npcs[npc->GetId()] = npc;
	}
	if (!_npcs.empty())
		LOG_INFO("Room", "마을 NPC: 마커 {}곳, 배치 {}명", _gameMap.GetNpcSpawns().size(), _npcs.size());
}

shared_ptr<TownNpc> Room::GetNpc(int npcId) const
{
	auto it = _npcs.find(npcId);
	return it != _npcs.end() ? it->second : nullptr;
}

void Room::UpdateNpcView(const shared_ptr<Player>& player)
{
	if (_npcs.empty()) return;

	shared_ptr<Session> session = player->GetSession();
	const Protocol::PositionInfo& pos = player->GetPosition();
	const float rIn = _gameMap.GetViewRadius();
	const float rOut = rIn + _gameMap.GetViewHysteresis();

	for (const auto& [npcId, npc] : _npcs)
	{
		const Protocol::PositionInfo& npcPos = npc->GetPosition();
		const float dx = npcPos.x() - pos.x(), dy = npcPos.y() - pos.y();
		const float distSq = dx * dx + dy * dy;
		if (player->npcView.count(npcId))
		{
			// 나감: R ~ R+H 사이는 그대로 둔다(경계 깜빡임 방지).
			if (distSq <= rOut * rOut) continue;
			player->npcView.erase(npcId);
			if (session) session->Send(PacketSerializer::MAKE_SC_REMOVE_OBJECT(npcId));
		}
		else if (distSq <= rIn * rIn)
		{
			player->npcView.insert(npcId);
			if (session) session->Send(PacketSerializer::MAKE_SC_ADD_OBJECT(npc));
		}
	}
}

Protocol::ItemResult Room::CheckShopRequest(const shared_ptr<Player>& player, int npcId, bool needItemOp, shared_ptr<TownNpc>& outNpc)
{
	// 검증 순서: 방 안 → 더미 봇 아님 → 생존 → 요청 간격(·진행 중인 작업) → 거래 중 아님 → NPC·상점 존재 → 거리
	if (false == IsInRoom(player)) return Protocol::ITEM_RESULT_NOT_IN_ROOM;
	if (player->isDummy || player->GetPlayerUID() == 0) return Protocol::ITEM_RESULT_NOT_ALLOWED;
	if (player->GetStat().IsDead()) return Protocol::ITEM_RESULT_DEAD;
	if (needItemOp)
	{
		const Protocol::ItemResult check = CheckItemRequest(player);
		if (check != Protocol::ITEM_RESULT_SUCCESS) return check;
	}
	else if (!player->itemOp.interval.IsReady())
	{
		return Protocol::ITEM_RESULT_TOO_FREQUENT;
	}
	// 1:1 거래가 열려 있으면(요청 대기 포함) 상점 전체를 막는다 — 올린 아이템만 막는 줍기·버리기와 다르다(D-201).
	if (player->tradeId != 0) return Protocol::ITEM_RESULT_IN_TRADE;

	const shared_ptr<TownNpc> npc = GetNpc(npcId);
	if (npc == nullptr || npc->GetShopId() == 0) return Protocol::ITEM_RESULT_NO_ITEM;

	const Protocol::PositionInfo& myPos = player->GetPosition();
	const Protocol::PositionInfo& npcPos = npc->GetPosition();
	const float dx = npcPos.x() - myPos.x(), dy = npcPos.y() - myPos.y();
	const float range = static_cast<float>(DataManager::GetEconomy().shopRangeCm);
	if (dx * dx + dy * dy > range * range) return Protocol::ITEM_RESULT_TOO_FAR;

	outNpc = npc;
	return Protocol::ITEM_RESULT_SUCCESS;
}

void Room::PlayerShopOpen(shared_ptr<Player> player, int npcId)
{
	if (player == nullptr) return;

	shared_ptr<TownNpc> npc;
	const Protocol::ItemResult check = CheckShopRequest(player, npcId, false, npc);
	if (check != Protocol::ITEM_RESULT_SUCCESS)
	{
		LOG_DEBUG("Item", "상점 열기 거부 player={} npc={} result={}", player->GetId(), npcId, Protocol::ItemResult_Name(check));
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_SHOP_OPEN, check));
		return;
	}

	player->itemOp.interval.Reset(DataManager::GetEconomy().itemMinIntervalMs);
	if (shared_ptr<Session> session = player->GetSession())
		session->Send(PacketSerializer::MAKE_SC_SHOP_OPEN(npcId, DataManager::GetShopItems(npc->GetShopId())));
}

void Room::PlayerShopBuy(shared_ptr<Player> player, int npcId, int itemId, int count)
{
	if (player == nullptr) return;

	auto reject = [&](Protocol::ItemResult result) {
		LOG_DEBUG("Item", "상점 구매 거부 player={} npc={} item={} count={} result={}", player->GetId(), npcId, itemId, count, Protocol::ItemResult_Name(result));
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_SHOP_BUY, result));
	};

	shared_ptr<TownNpc> npc;
	const Protocol::ItemResult check = CheckShopRequest(player, npcId, true, npc);
	if (check != Protocol::ITEM_RESULT_SUCCESS) { reject(check); return; }

	// 품목 → 수량 → 골드 → 빈 칸
	const ShopData* row = DataManager::FindShopItem(npc->GetShopId(), itemId);
	const ItemData* item = row != nullptr ? DataManager::GetItemData(itemId) : nullptr;
	if (row == nullptr || item == nullptr || count < 1) { reject(Protocol::ITEM_RESULT_NO_ITEM); return; }

	const EconomyData& economy = DataManager::GetEconomy();
	// 수량 상한(MaxStack × 칸 수)을 먼저 본다 — 아래 총액 계산이 넘치지 않게(PlanAdd도 같은 상한을 본다).
	if (static_cast<int64_t>(count) > static_cast<int64_t>(item->maxStack) * economy.inventorySlots) { reject(Protocol::ITEM_RESULT_INVENTORY_FULL); return; }
	const int64_t total = static_cast<int64_t>(row->buyPrice) * count;
	if (total > player->GetGold()) { reject(Protocol::ITEM_RESULT_NOT_ENOUGH_GOLD); return; }

	DB_ItemOp op;
	op.kind = ItemOpKind::ShopBuy;
	op.itemId = itemId;
	// 덜 찬 스택부터 채우고 남으면 새 칸. 전부 들어갈 자리가 없으면 통째로 거부한다(D-155).
	if (false == player->inventory.PlanAdd(itemId, count, item->maxStack, economy.inventorySlots, op.fills, op.newCounts, op.newSlots)) { reject(Protocol::ITEM_RESULT_INVENTORY_FULL); return; }
	op.goldDelta = -total;
	op.goldMax = economy.goldMax;

	player->itemOp.interval.Reset(economy.itemMinIntervalMs);
	StartItemOp(player, op);
}

void Room::PlayerShopSell(shared_ptr<Player> player, int npcId, int64_t itemDbId, int count)
{
	if (player == nullptr) return;

	auto reject = [&](Protocol::ItemResult result) {
		LOG_DEBUG("Item", "상점 판매 거부 player={} npc={} dbId={} count={} result={}", player->GetId(), npcId, itemDbId, count, Protocol::ItemResult_Name(result));
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_SHOP_SELL, result));
	};

	shared_ptr<TownNpc> npc;
	const Protocol::ItemResult check = CheckShopRequest(player, npcId, true, npc);
	if (check != Protocol::ITEM_RESULT_SUCCESS) { reject(check); return; }

	// 인벤토리 위치의 내 행만 판다(장착 중인 것은 먼저 해제해야 한다). 스택은 일부만 팔 수 있다.
	const ItemInstance* owned = player->inventory.Find(itemDbId);
	if (owned == nullptr || owned->equipPart != 0 || count < 1 || count > owned->count) { reject(Protocol::ITEM_RESULT_NO_ITEM); return; }

	const ItemData* item = DataManager::GetItemData(owned->itemId);
	if (item == nullptr || item->sellPrice <= 0) { reject(Protocol::ITEM_RESULT_NOT_SELLABLE); return; }

	const EconomyData& economy = DataManager::GetEconomy();
	const int64_t total = static_cast<int64_t>(item->sellPrice) * count;
	if (total > static_cast<int64_t>(economy.goldMax) - player->GetGold()) { reject(Protocol::ITEM_RESULT_GOLD_FULL); return; }

	DB_ItemOp op;
	op.kind = ItemOpKind::ShopSell;
	op.itemId = owned->itemId;
	op.sellDbId = itemDbId;
	op.sellExpectCount = owned->count;
	op.sellCount = count;
	op.goldDelta = total;
	op.goldMax = economy.goldMax;

	player->itemOp.interval.Reset(economy.itemMinIntervalMs);
	StartItemOp(player, op);
}

// ---------------------------------------------------------------------------------------------------------------
// 경매장 (A4.3, D-159·D-234·D-235 — DESIGN_ITEMS_TRADE "경매장")
// ---------------------------------------------------------------------------------------------------------------
namespace
{
	// 거부를 본인에게 알린다. 성공은 결과 적용(ApplyItemOpResult)에서 보낸다.
	void SendAuctionReject(const shared_ptr<Player>& player, Protocol::PacketId request, Protocol::AuctionResult result, int64_t id)
	{
		Metrics::AuctionRejected.fetch_add(1);
		LOG_DEBUG("Auction", "거부 player={} request={} id={} result={}", player->GetId(), Protocol::PacketId_Name(request), id, Protocol::AuctionResult_Name(result));
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_AUCTION_RESULT(request, result, id));
	}

	Protocol::AuctionResult ToAuctionResult(Protocol::ItemResult result)
	{
		switch (result)
		{
		case Protocol::ITEM_RESULT_SUCCESS: return Protocol::AUCTION_RESULT_SUCCESS;
		case Protocol::ITEM_RESULT_NOT_IN_ROOM: return Protocol::AUCTION_RESULT_NOT_IN_ROOM;
		case Protocol::ITEM_RESULT_TOO_FREQUENT: return Protocol::AUCTION_RESULT_TOO_FREQUENT;
		case Protocol::ITEM_RESULT_BUSY: return Protocol::AUCTION_RESULT_BUSY;
		default: return Protocol::AUCTION_RESULT_NOT_ALLOWED;
		}
	}
}

Protocol::AuctionResult Room::CheckAuctionRequest(const shared_ptr<Player>& player, AuctionRequest kind, int npcId)
{
	if (false == IsInRoom(player)) return Protocol::AUCTION_RESULT_NOT_IN_ROOM;
	// 더미 봇은 전원이 DB 한 행을 쓴다 — 검색(DB 행을 건드리지 않음)만 받는다. 부하 시험의 조회 부하용(D-235).
	const bool isDummy = player->isDummy || player->GetPlayerUID() == 0;
	if (isDummy && kind != AuctionRequest::Search) return Protocol::AUCTION_RESULT_NOT_ALLOWED;
	if (player->GetStat().IsDead()) return Protocol::AUCTION_RESULT_DEAD;
	// 던전 안에서는 경매를 쓸 수 없다(D-234).
	if (_info.kind == RoomKind::Dungeon) return Protocol::AUCTION_RESULT_IN_DUNGEON;

	if (kind == AuctionRequest::Search || kind == AuctionRequest::My)
	{
		if (!player->auctionQuery.IsReady()) return Protocol::AUCTION_RESULT_TOO_FREQUENT;
		return Protocol::AUCTION_RESULT_SUCCESS;
	}

	// 아이템·골드가 바뀌는 요청: 요청 간격 → Room 이동 중·진행 중인 작업(BUSY)
	const Protocol::ItemResult check = CheckItemRequest(player);
	if (check != Protocol::ITEM_RESULT_SUCCESS) return ToAuctionResult(check);

	if (kind == AuctionRequest::NpcChange)
	{
		// 등록·취소·수령은 경매 NPC 근처에서만(D-234). 서버에 "창을 연 상태"는 없고 요청마다 본다(상점과 같음).
		const shared_ptr<TownNpc> npc = GetNpc(npcId);
		if (npc == nullptr || !npc->IsAuction()) return Protocol::AUCTION_RESULT_TOO_FAR;

		const Protocol::PositionInfo& myPos = player->GetPosition();
		const Protocol::PositionInfo& npcPos = npc->GetPosition();
		const float dx = npcPos.x() - myPos.x(), dy = npcPos.y() - myPos.y();
		const float range = static_cast<float>(DataManager::GetEconomy().shopRangeCm);
		if (dx * dx + dy * dy > range * range) return Protocol::AUCTION_RESULT_TOO_FAR;
	}
	return Protocol::AUCTION_RESULT_SUCCESS;
}

void Room::PlayerAuctionSearch(shared_ptr<Player> player, int slot, int page)
{
	if (player == nullptr) return;

	const Protocol::AuctionResult check = CheckAuctionRequest(player, AuctionRequest::Search, 0);
	if (check != Protocol::AUCTION_RESULT_SUCCESS) { SendAuctionReject(player, Protocol::CS_AUCTION_SEARCH, check, 0); return; }
	if (slot < 0 || slot >= AuctionService::FILTER_COUNT || page < 0) { SendAuctionReject(player, Protocol::CS_AUCTION_SEARCH, Protocol::AUCTION_RESULT_NOT_ALLOWED, 0); return; }

	const shared_ptr<GameSession> session = player->GetSession();
	if (session == nullptr) return;

	player->auctionQuery.Reset(DataManager::GetEconomy().itemMinIntervalMs);
	GAuction->PushJob(&AuctionService::Search, session, player->GetPlayerUID(), slot, page);
}

void Room::PlayerAuctionMy(shared_ptr<Player> player)
{
	if (player == nullptr) return;

	const Protocol::AuctionResult check = CheckAuctionRequest(player, AuctionRequest::My, 0);
	if (check != Protocol::AUCTION_RESULT_SUCCESS) { SendAuctionReject(player, Protocol::CS_AUCTION_MY, check, 0); return; }

	const shared_ptr<GameSession> session = player->GetSession();
	if (session == nullptr) return;

	// 수령함은 DB에서 읽는다 — 혼잡 기준(D-235)에 든다. 서비스가 답을 보낼 때(AuctionService::OnMailRead) 진행 수를 줄인다.
	if (!AuctionService::TryBeginDbWork()) { SendAuctionReject(player, Protocol::CS_AUCTION_MY, Protocol::AUCTION_RESULT_TRY_LATER, 0); return; }

	player->auctionQuery.Reset(DataManager::GetEconomy().itemMinIntervalMs);
	GAuction->PushJob(&AuctionService::QueryMy, session, player->GetPlayerUID());
}

void Room::PlayerAuctionRegister(shared_ptr<Player> player, int npcId, int64_t itemDbId, int count, int64_t price)
{
	if (player == nullptr) return;
	auto reject = [&](Protocol::AuctionResult result) { SendAuctionReject(player, Protocol::CS_AUCTION_REGISTER, result, 0); };

	const Protocol::AuctionResult check = CheckAuctionRequest(player, AuctionRequest::NpcChange, npcId);
	if (check != Protocol::AUCTION_RESULT_SUCCESS) { reject(check); return; }

	// 인벤토리 위치의 내 행만 올린다(장착 중인 것은 먼저 해제해야 한다). 스택은 일부만 올릴 수 있다.
	const ItemInstance* owned = player->inventory.Find(itemDbId);
	if (owned == nullptr || owned->equipPart != 0 || count < 1 || count > owned->count) { reject(Protocol::AUCTION_RESULT_NO_ITEM); return; }
	if (DataManager::GetItemData(owned->itemId) == nullptr) { reject(Protocol::AUCTION_RESULT_NO_ITEM); return; }
	// 1:1 거래에 올린 행은 일부만 올렸어도 등록할 수 없다.
	if (IsItemInTrade(player, itemDbId)) { reject(Protocol::AUCTION_RESULT_IN_TRADE); return; }

	const EconomyData& economy = DataManager::GetEconomy();
	if (price < economy.auctionMinPrice || price > economy.auctionMaxPrice) { reject(Protocol::AUCTION_RESULT_BAD_PRICE); return; }

	// 혼잡 기준(D-235). 여기를 지나면 결과 적용(ApplyItemOpResult)에서 진행 수를 줄인다.
	if (!AuctionService::TryBeginDbWork()) { reject(Protocol::AUCTION_RESULT_TRY_LATER); return; }

	DB_ItemOp op;
	op.kind = ItemOpKind::AuctionRegister;
	op.itemId = owned->itemId;
	op.auctionDbId = itemDbId;
	op.auctionExpectCount = owned->count;
	op.auctionCount = count;
	op.auctionSlot = owned->slot;
	op.auctionPrice = price;
	op.auctionDurationSec = economy.auctionDurationSec;

	player->itemOp.interval.Reset(economy.itemMinIntervalMs);
	StartItemOp(player, op);
}

void Room::PlayerAuctionBuy(shared_ptr<Player> player, int64_t listingId)
{
	if (player == nullptr) return;
	auto reject = [&](Protocol::AuctionResult result) { SendAuctionReject(player, Protocol::CS_AUCTION_BUY, result, listingId); };

	const Protocol::AuctionResult check = CheckAuctionRequest(player, AuctionRequest::Change, 0);
	if (check != Protocol::AUCTION_RESULT_SUCCESS) { reject(check); return; }
	if (listingId <= 0) { reject(Protocol::AUCTION_RESULT_NO_LISTING); return; }

	if (!AuctionService::TryBeginDbWork()) { reject(Protocol::AUCTION_RESULT_TRY_LATER); return; }

	// 등록의 존재·값·판매자는 서비스의 목록이 안다. 여기서는 보유 골드를 실어 보내 서비스가 값과 비교하게 한다.
	// 산 물건은 수령함으로 가므로 빈 칸은 보지 않는다.
	DB_ItemOp op;
	op.kind = ItemOpKind::AuctionBuy;
	op.listingId = listingId;
	op.auctionGold = player->GetGold();

	player->itemOp.interval.Reset(DataManager::GetEconomy().itemMinIntervalMs);
	StartItemOp(player, op);
}

void Room::PlayerAuctionCancel(shared_ptr<Player> player, int npcId, int64_t listingId)
{
	if (player == nullptr) return;
	auto reject = [&](Protocol::AuctionResult result) { SendAuctionReject(player, Protocol::CS_AUCTION_CANCEL, result, listingId); };

	const Protocol::AuctionResult check = CheckAuctionRequest(player, AuctionRequest::NpcChange, npcId);
	if (check != Protocol::AUCTION_RESULT_SUCCESS) { reject(check); return; }
	if (listingId <= 0) { reject(Protocol::AUCTION_RESULT_NO_LISTING); return; }

	if (!AuctionService::TryBeginDbWork()) { reject(Protocol::AUCTION_RESULT_TRY_LATER); return; }

	DB_ItemOp op;
	op.kind = ItemOpKind::AuctionCancel;
	op.listingId = listingId;

	player->itemOp.interval.Reset(DataManager::GetEconomy().itemMinIntervalMs);
	StartItemOp(player, op);
}

void Room::PlayerMailClaim(shared_ptr<Player> player, int npcId, int64_t mailId)
{
	if (player == nullptr) return;
	auto reject = [&](Protocol::AuctionResult result) { SendAuctionReject(player, Protocol::CS_MAIL_CLAIM, result, mailId); };

	const Protocol::AuctionResult check = CheckAuctionRequest(player, AuctionRequest::NpcChange, npcId);
	if (check != Protocol::AUCTION_RESULT_SUCCESS) { reject(check); return; }
	if (mailId <= 0) { reject(Protocol::AUCTION_RESULT_NO_MAIL); return; }

	if (!AuctionService::TryBeginDbWork()) { reject(Protocol::AUCTION_RESULT_TRY_LATER); return; }

	// 우편이 아이템인지 골드인지는 메모리에 없다(수령함은 DB에만 있다). 아이템일 때 들어갈 빈 칸을 골라 보내고,
	// 빈 칸이 없으면 -1을 보낸다 — 골드 우편이면 그대로 받고, 아이템 우편이면 DB가 INVENTORY_FULL로 거부한다. 받은 아이템은 스택에 합치지 않는다.
	const EconomyData& economy = DataManager::GetEconomy();
	const vector<int> freeSlots = player->inventory.FreeSlots(economy.inventorySlots, 1);

	DB_ItemOp op;
	op.kind = ItemOpKind::MailClaim;
	op.mailId = mailId;
	op.claimSlot = freeSlots.empty() ? -1 : freeSlots[0];
	op.goldMax = economy.goldMax;

	player->itemOp.interval.Reset(economy.itemMinIntervalMs);
	StartItemOp(player, op);
}

// ---------------------------------------------------------------------------------------------------------------
// 1:1 거래 (A4.1·A4.2, D-158 — DESIGN_ITEMS_TRADE "1:1 거래")
// ---------------------------------------------------------------------------------------------------------------
namespace
{
	void SendTradeReject(const shared_ptr<Player>& player, Protocol::PacketId request, Protocol::TradeResult result)
	{
		LOG_DEBUG("Trade", "거부 player={} request={} result={}", player->GetId(), Protocol::PacketId_Name(request), Protocol::TradeResult_Name(result));
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_TRADE_RESULT(request, result));
	}

	long long TradeNowMs()
	{
		return chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	}
}

TradeSession* Room::FindTrade(const shared_ptr<Player>& player)
{
	if (player == nullptr || player->tradeId == 0) return nullptr;
	const auto it = _trades.find(player->tradeId);
	if (it == _trades.end()) return nullptr;
	// 같은 id를 다른 거래가 쓰지 않지만, 포인터로 한 번 더 확인한다.
	if (it->second.sides[0].player != player && it->second.sides[1].player != player) return nullptr;
	return &it->second;
}

const TradeSession* Room::FindTrade(const shared_ptr<Player>& player) const
{
	return const_cast<Room*>(this)->FindTrade(player);
}

bool Room::IsItemInTrade(const shared_ptr<Player>& player, int64_t itemDbId) const
{
	const TradeSession* trade = FindTrade(player);
	if (trade == nullptr) return false;
	const TradeSide& mine = trade->sides[trade->sides[0].player == player ? 0 : 1];
	for (const TradeItem& item : mine.items)
		if (item.itemDbId == itemDbId) return true;
	return false;
}

bool Room::IsTradeRangeOk(const TradeSession& trade) const
{
	const Protocol::PositionInfo& a = trade.sides[0].player->GetPosition();
	const Protocol::PositionInfo& b = trade.sides[1].player->GetPosition();
	const float dx = a.x() - b.x(), dy = a.y() - b.y();
	const float range = static_cast<float>(DataManager::GetEconomy().tradeRangeCm);
	return dx * dx + dy * dy <= range * range;
}

void Room::SendTradeState(const TradeSession& trade)
{
	const shared_ptr<SendBuffer> buffer = PacketSerializer::MAKE_SC_TRADE_STATE(trade);
	for (const TradeSide& side : trade.sides)
	{
		if (!IsInRoom(side.player)) continue;
		if (shared_ptr<Session> session = side.player->GetSession())
			session->Send(buffer);
	}
}

void Room::EndTrade(int tradeId, Protocol::TradeResult result)
{
	const auto it = _trades.find(tradeId);
	if (it == _trades.end()) return;

	LOG_DEBUG("Trade", "끝 id={} a={} b={} result={}", tradeId, it->second.sides[0].objectId, it->second.sides[1].objectId, Protocol::TradeResult_Name(result));
	const shared_ptr<SendBuffer> buffer = PacketSerializer::MAKE_SC_TRADE_END(result);
	for (TradeSide& side : it->second.sides)
	{
		if (side.player->tradeId == tradeId) side.player->tradeId = 0;
		if (!IsInRoom(side.player)) continue;
		if (shared_ptr<Session> session = side.player->GetSession())
			session->Send(buffer);
	}
	_trades.erase(it);
}

void Room::CancelTradeOf(const shared_ptr<Player>& player, Protocol::TradeResult reason)
{
	TradeSession* trade = FindTrade(player);
	if (trade == nullptr) return;
	// 확정 중이면 DB 결과가 권위다. 결과 처리(OnTradeDone)가 남은 쪽에만 반영하고 끝낸다.
	if (trade->phase == Protocol::TRADE_PHASE_CONFIRMING) return;
	EndTrade(trade->id, reason);
}

void Room::TradeRequest(shared_ptr<Player> player, int targetId)
{
	if (player == nullptr) return;
	const Protocol::PacketId req = Protocol::CS_TRADE_REQUEST;

	if (false == IsInRoom(player)) { SendTradeReject(player, req, Protocol::TRADE_RESULT_NOT_ALLOWED); return; }
	if (player->isDummy || player->GetPlayerUID() == 0) { SendTradeReject(player, req, Protocol::TRADE_RESULT_NOT_ALLOWED); return; }
	if (player->GetStat().IsDead()) { SendTradeReject(player, req, Protocol::TRADE_RESULT_DEAD); return; }
	if (player->tradeId != 0) { SendTradeReject(player, req, Protocol::TRADE_RESULT_BUSY); return; }
	// Room 이동 대기·이동 중에는 거래를 시작하지 않는다(DG3b).
	if (player->roomMove.Active()) { SendTradeReject(player, req, Protocol::TRADE_RESULT_BUSY); return; }
	// 요청 도배 방지: 아이템 요청과 같은 최소 간격을 쓴다.
	if (!player->itemOp.interval.IsReady()) { SendTradeReject(player, req, Protocol::TRADE_RESULT_BUSY); return; }

	const shared_ptr<Player> target = Id2Player(targetId);
	if (target == nullptr || target == player || target->isDummy || target->GetPlayerUID() == 0 || target->GetStat().IsDead())
	{
		SendTradeReject(player, req, Protocol::TRADE_RESULT_NO_TARGET);
		return;
	}
	if (target->tradeId != 0 || target->roomMove.Active()) { SendTradeReject(player, req, Protocol::TRADE_RESULT_TARGET_BUSY); return; }

	const EconomyData& economy = DataManager::GetEconomy();
	TradeSession trade;
	trade.sides[0].player = player; trade.sides[0].playerUID = player->GetPlayerUID(); trade.sides[0].objectId = player->GetId();
	trade.sides[1].player = target; trade.sides[1].playerUID = target->GetPlayerUID(); trade.sides[1].objectId = target->GetId();
	if (!IsTradeRangeOk(trade)) { SendTradeReject(player, req, Protocol::TRADE_RESULT_TOO_FAR); return; }

	do { _nextTradeId = _nextTradeId >= INT_MAX ? 1 : _nextTradeId + 1; } while (_trades.count(_nextTradeId) > 0);
	trade.id = _nextTradeId;
	trade.phase = Protocol::TRADE_PHASE_REQUESTING;
	trade.requestedAtMs = TradeNowMs();
	player->tradeId = trade.id;
	target->tradeId = trade.id;
	player->itemOp.interval.Reset(economy.itemMinIntervalMs);
	const auto inserted = _trades.emplace(trade.id, std::move(trade));

	LOG_DEBUG("Trade", "요청 id={} from={} to={}", inserted.first->first, player->GetId(), target->GetId());
	if (shared_ptr<Session> session = target->GetSession())
		session->Send(PacketSerializer::MAKE_SC_TRADE_REQUEST(player->GetId(), player->GetName(), economy.tradeRequestTimeoutSec));
	SendTradeState(inserted.first->second);
}

void Room::TradeRespond(shared_ptr<Player> player, bool accept)
{
	if (player == nullptr) return;
	TradeSession* trade = FindTrade(player);
	// 받은 요청이 없거나(이미 취소·시간 초과), 내가 요청한 쪽이거나, 이미 수락한 거래다.
	if (trade == nullptr || trade->phase != Protocol::TRADE_PHASE_REQUESTING || trade->sides[1].player != player)
	{
		SendTradeReject(player, Protocol::CS_TRADE_RESPOND, Protocol::TRADE_RESULT_NOT_ALLOWED);
		return;
	}

	if (!accept) { EndTrade(trade->id, Protocol::TRADE_RESULT_DECLINED); return; }

	trade->phase = Protocol::TRADE_PHASE_OFFERING;
	trade->acceptedAtMs = TradeNowMs();
	SendTradeState(*trade);
}

void Room::TradeSetOffer(shared_ptr<Player> player, vector<TradeItem> items, int64_t gold, bool tooMany)
{
	if (player == nullptr) return;
	const Protocol::PacketId req = Protocol::CS_TRADE_SET_OFFER;
	TradeSession* trade = FindTrade(player);
	if (trade == nullptr || trade->phase != Protocol::TRADE_PHASE_OFFERING) { SendTradeReject(player, req, Protocol::TRADE_RESULT_NOT_ALLOWED); return; }

	TradeSide& mine = trade->sides[trade->sides[0].player == player ? 0 : 1];
	// 잠근 뒤에는 바꿀 수 없다. 바꾸려면 먼저 풀어야 한다(풀면 양쪽 잠금·확정이 풀린다).
	if (mine.locked) { SendTradeReject(player, req, Protocol::TRADE_RESULT_LOCKED); return; }
	// 내 아이템 작업이 진행 중이면 다른 경로로 빠져나가는 중인 아이템일 수 있다.
	if (player->itemOp.inFlight) { SendTradeReject(player, req, Protocol::TRADE_RESULT_BUSY); return; }

	const EconomyData& economy = DataManager::GetEconomy();
	bool valid = !tooMany && static_cast<int>(items.size()) <= economy.tradeMaxItems && gold >= 0 && gold <= player->GetGold();
	for (size_t i = 0; valid && i < items.size(); ++i)
	{
		const ItemInstance* owned = player->inventory.Find(items[i].itemDbId);
		// 인벤토리 위치의 내 행, 수량 1 ~ 그 행의 수량, 같은 행은 한 번만
		if (owned == nullptr || owned->equipPart != 0 || items[i].count < 1 || items[i].count > owned->count) { valid = false; break; }
		for (size_t j = 0; j < i; ++j)
			if (items[j].itemDbId == items[i].itemDbId) { valid = false; break; }
		items[i].itemId = owned->itemId;
	}
	if (!valid) { SendTradeReject(player, req, Protocol::TRADE_RESULT_INVALID_OFFER); return; }

	mine.items = std::move(items);
	mine.gold = gold;
	// 제시가 바뀌면 상대가 보고 잠근 내용이 달라진다. 양쪽의 잠금·확정을 모두 푼다.
	for (TradeSide& side : trade->sides) { side.locked = false; side.confirmed = false; }
	SendTradeState(*trade);
}

void Room::TradeLock(shared_ptr<Player> player, bool locked)
{
	if (player == nullptr) return;
	TradeSession* trade = FindTrade(player);
	if (trade == nullptr || trade->phase != Protocol::TRADE_PHASE_OFFERING) { SendTradeReject(player, Protocol::CS_TRADE_LOCK, Protocol::TRADE_RESULT_NOT_ALLOWED); return; }

	TradeSide& mine = trade->sides[trade->sides[0].player == player ? 0 : 1];
	if (locked)
	{
		if (mine.locked) return;   // 이미 잠갔다
		mine.locked = true;
	}
	else
	{
		// 누가 풀면 양쪽의 잠금과 확정이 모두 풀린다(잠근 뒤 몰래 바꿀 수 없게).
		for (TradeSide& side : trade->sides) { side.locked = false; side.confirmed = false; }
	}
	SendTradeState(*trade);
}

void Room::TradeConfirm(shared_ptr<Player> player)
{
	if (player == nullptr) return;
	TradeSession* trade = FindTrade(player);
	// 양쪽이 모두 잠근 뒤에만 확정할 수 있다.
	if (trade == nullptr || trade->phase != Protocol::TRADE_PHASE_OFFERING || !trade->sides[0].locked || !trade->sides[1].locked)
	{
		SendTradeReject(player, Protocol::CS_TRADE_CONFIRM, Protocol::TRADE_RESULT_NOT_ALLOWED);
		return;
	}

	TradeSide& mine = trade->sides[trade->sides[0].player == player ? 0 : 1];
	if (mine.confirmed) return;   // 두 번째 확정은 무시한다
	mine.confirmed = true;

	if (trade->sides[0].confirmed && trade->sides[1].confirmed)
		TryExecuteTrade(*trade);
	else
		SendTradeState(*trade);
}

void Room::TradeCancel(shared_ptr<Player> player)
{
	if (player == nullptr) return;
	TradeSession* trade = FindTrade(player);
	if (trade == nullptr) { SendTradeReject(player, Protocol::CS_TRADE_CANCEL, Protocol::TRADE_RESULT_NOT_ALLOWED); return; }
	// 확정 중에는 DB 결과를 기다린다(되돌릴 수 없다).
	if (trade->phase == Protocol::TRADE_PHASE_CONFIRMING) { SendTradeReject(player, Protocol::CS_TRADE_CANCEL, Protocol::TRADE_RESULT_BUSY); return; }
	EndTrade(trade->id, Protocol::TRADE_RESULT_CANCELLED);
}

void Room::TryExecuteTrade(TradeSession& trade)
{
	const EconomyData& economy = DataManager::GetEconomy();
	const shared_ptr<Player>& a = trade.sides[0].player;
	const shared_ptr<Player>& b = trade.sides[1].player;

	// 다시 검증한다: 확정 사이에 인벤토리·골드·위치·생존이 바뀌었을 수 있다.
	Protocol::TradeResult fail = Protocol::TRADE_RESULT_SUCCESS;
	DB_ItemOp op;
	op.kind = ItemOpKind::Trade;
	op.tradeId = trade.id;
	op.tradeOtherUID = trade.sides[1].playerUID;
	op.tradeGoldA = trade.sides[0].gold;
	op.tradeGoldB = trade.sides[1].gold;
	op.goldMax = economy.goldMax;

	auto connected = [&](const shared_ptr<Player>& p) {
		const shared_ptr<GameSession> session = p->GetSession();
		return IsInRoom(p) && session != nullptr && session->IsConnected();
	};

	if (!connected(a) || !connected(b)) fail = Protocol::TRADE_RESULT_DISCONNECTED;
	else if (a->GetStat().IsDead() || b->GetStat().IsDead()) fail = Protocol::TRADE_RESULT_DEAD;
	else if (!IsTradeRangeOk(trade)) fail = Protocol::TRADE_RESULT_TOO_FAR;
	else if (a->itemOp.inFlight || b->itemOp.inFlight) fail = Protocol::TRADE_RESULT_BUSY;

	if (fail == Protocol::TRADE_RESULT_SUCCESS)
	{
		for (int s = 0; s < 2 && fail == Protocol::TRADE_RESULT_SUCCESS; ++s)
		{
			const TradeSide& giver = trade.sides[s];
			const shared_ptr<Player>& receiver = trade.sides[1 - s].player;
			const TradeSide& receiverSide = trade.sides[1 - s];

			// 골드: 내놓을 만큼 있고, 받은 뒤 상대가 상한을 넘지 않는다.
			if (giver.gold < 0 || giver.gold > giver.player->GetGold()) { fail = Protocol::TRADE_RESULT_INVALID_OFFER; break; }
			if (receiver->GetGold() - receiverSide.gold + giver.gold > static_cast<int64_t>(economy.goldMax)) { fail = Protocol::TRADE_RESULT_GOLD_FULL; break; }

			// 받는 쪽이 쓸 수 있는 칸 = 지금 빈 칸 + 받는 쪽이 통째로 내놓아 비게 될 칸. 받은 아이템은 기존 스택에 합치지 않고 한 칸씩 차지한다.
			vector<int> slots = receiver->inventory.FreeSlots(economy.inventorySlots, economy.inventorySlots);
			for (const TradeItem& out : receiverSide.items)
			{
				const ItemInstance* owned = receiver->inventory.Find(out.itemDbId);
				if (owned != nullptr && owned->equipPart == 0 && out.count == owned->count && owned->slot >= 0 && owned->slot < economy.inventorySlots)
					slots.push_back(owned->slot);
			}
			sort(slots.begin(), slots.end());
			if (slots.size() < giver.items.size()) { fail = Protocol::TRADE_RESULT_INVENTORY_FULL; break; }

			for (size_t i = 0; i < giver.items.size(); ++i)
			{
				const TradeItem& offered = giver.items[i];
				const ItemInstance* owned = giver.player->inventory.Find(offered.itemDbId);
				// 아직 인벤토리에 있고 수량이 제시 수량 이상이어야 한다(줍기로 늘어난 것은 괜찮다 — 넘어가는 것은 제시한 수량뿐).
				if (owned == nullptr || owned->equipPart != 0 || owned->itemId != offered.itemId || owned->count < offered.count) { fail = Protocol::TRADE_RESULT_INVALID_OFFER; break; }
				DB_ItemOp::TradeMove move;
				move.fromSide = s;
				move.itemDbId = offered.itemDbId;
				move.itemId = offered.itemId;
				move.count = static_cast<int32_t>(offered.count);
				move.expectCount = owned->count;
				move.toSlot = slots[i];
				op.tradeMoves.push_back(move);
			}
		}
	}

	if (fail != Protocol::TRADE_RESULT_SUCCESS)
	{
		// 거래를 끝내지 않는다: 양쪽 잠금·확정을 풀어 제시 단계로 되돌리고 사유를 두 사람에게 알린다.
		LOG_DEBUG("Trade", "확정 재검증 실패 id={} result={}", trade.id, Protocol::TradeResult_Name(fail));
		for (TradeSide& side : trade.sides) { side.locked = false; side.confirmed = false; }
		const shared_ptr<SendBuffer> buffer = PacketSerializer::MAKE_SC_TRADE_RESULT(Protocol::CS_TRADE_CONFIRM, fail);
		for (const TradeSide& side : trade.sides)
		{
			if (!IsInRoom(side.player)) continue;
			if (shared_ptr<Session> session = side.player->GetSession())
				session->Send(buffer);
		}
		SendTradeState(trade);
		return;
	}

	// 확정 중: DB 결과가 올 때까지 양쪽의 다른 아이템 요청은 BUSY, 거래 요청은 모두 거부한다.
	trade.phase = Protocol::TRADE_PHASE_CONFIRMING;
	SendTradeState(trade);
	b->itemOp.inFlight = true;    // A는 StartItemOp가 켠다. B는 여기서 켜고 OnTradeDone에서 끈다.
	StartItemOp(a, op);
}

void Room::OnTradeDone(const DB_ItemOp& op, const DB_ItemOpResult& result)
{
	const auto it = _trades.find(op.tradeId);
	if (it == _trades.end())
	{
		LOG_ERROR("Trade", "DB 결과를 받았는데 거래 상태가 없습니다 (id {})", op.tradeId);
		return;
	}
	TradeSession& trade = it->second;
	const shared_ptr<Player> a = trade.sides[0].player;
	const shared_ptr<Player> b = trade.sides[1].player;
	b->itemOp.inFlight = false;   // A는 OnItemOpDone이 이미 껐다

	if (result.success)
	{
		// DB가 권위다. 방에 남아 있는 쪽의 메모리에만 반영한다(나간 쪽은 다음 로그인 때 DB에서 읽는다).
		vector<ItemInstance> changed[2];
		vector<int64_t> removed[2];
		size_t newIndex = 0;
		for (const DB_ItemOp::TradeMove& m : op.tradeMoves)
		{
			const int from = m.fromSide, to = 1 - m.fromSide;
			const shared_ptr<Player>& giver = trade.sides[from].player;
			const shared_ptr<Player>& receiver = trade.sides[to].player;
			const bool whole = m.count == m.expectCount;
			int64_t receivedDbId = m.itemDbId;
			if (!whole)
			{
				// 일부만 넘긴 행: 받는 쪽은 새 행(새 ItemDbId)
				receivedDbId = newIndex < result.newDbIds.size() ? result.newDbIds[newIndex] : 0;
				++newIndex;
			}

			if (IsInRoom(giver))
			{
				if (whole)
				{
					giver->inventory.Remove(m.itemDbId);
					removed[from].push_back(m.itemDbId);
				}
				else if (const ItemInstance* owned = giver->inventory.Find(m.itemDbId))
				{
					ItemInstance left = *owned;
					left.count = m.expectCount - m.count;
					giver->inventory.Set(left);
					changed[from].push_back(left);
				}
			}
			if (IsInRoom(receiver) && receivedDbId != 0)
			{
				const ItemInstance received{ receivedDbId, m.itemId, m.count, 0, m.toSlot };
				receiver->inventory.Set(received);
				changed[to].push_back(received);
			}
		}
		if (newIndex != result.newDbIds.size())
			LOG_ERROR("Trade", "거래 결과의 새 행 수가 다릅니다 (id {}, 기대 {}, 받음 {}) — 다음 로그인 때 DB 값으로 맞춰집니다", op.tradeId, newIndex, result.newDbIds.size());

		if (result.gold >= 0 && IsInRoom(a)) a->SetGold(result.gold);
		if (result.gold2 >= 0 && IsInRoom(b)) b->SetGold(result.gold2);

		for (int s = 0; s < 2; ++s)
		{
			const shared_ptr<Player>& p = trade.sides[s].player;
			if (!IsInRoom(p)) continue;
			if (shared_ptr<Session> session = p->GetSession())
				session->Send(PacketSerializer::MAKE_SC_INVENTORY_UPDATE(changed[s], removed[s], p->GetGold()));
		}
		Metrics::TradesDone.fetch_add(1);
	}
	else if (result.unknown)
	{
		// DB에 반영됐는지 알 수 없다. A는 OnItemOpDone이 끊었다. B도 끊어 다음 로그인 때 DB 값을 읽게 한다.
		if (IsInRoom(b))
		{
			if (shared_ptr<GameSession> session = b->GetSession())
				session->ForceClose(L"Trade: unknown DB result");
		}
	}

	EndTrade(op.tradeId, result.success ? Protocol::TRADE_RESULT_SUCCESS : Protocol::TRADE_RESULT_DB_ERROR);
	// B가 이 거래의 결과를 기다리던 Room 이동이 있으면 이어 간다(A는 OnItemOpDone 끝에서 — DG3b).
	TryAdvanceMove(b);
	// B가 이미 방을 나갔으면 마지막 DB 작업이 끝났으니 접속 목록에서 빼게 한다(A는 OnItemOpDone 끝에서 한다).
	NotifyClosedIfDone(b);
}

void Room::UpdateTrades(long long nowMs)
{
	if (_trades.empty()) return;
	const EconomyData& economy = DataManager::GetEconomy();
	vector<int> expired;
	for (const auto& [id, trade] : _trades)
	{
		if (trade.phase == Protocol::TRADE_PHASE_REQUESTING)
		{
			if (nowMs - trade.requestedAtMs >= static_cast<long long>(economy.tradeRequestTimeoutSec) * 1000) expired.push_back(id);
		}
		else if (trade.phase == Protocol::TRADE_PHASE_OFFERING)
		{
			if (nowMs - trade.acceptedAtMs >= static_cast<long long>(economy.tradeTimeoutSec) * 1000) expired.push_back(id);
		}
	}
	for (const int id : expired)
		EndTrade(id, Protocol::TRADE_RESULT_TIMEOUT);
}

void Room::PlayerUseItem(shared_ptr<Player> player, int64_t itemDbId, int itemId)
{
	if (player == nullptr) return;

	auto reject = [&](Protocol::ItemResult result) {
		LOG_DEBUG("Item", "사용 거부 player={} dbId={} item={} result={}", player->GetId(), itemDbId, itemId, Protocol::ItemResult_Name(result));
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_USE_ITEM, result));
	};

	if (false == IsInRoom(player)) { reject(Protocol::ITEM_RESULT_NOT_IN_ROOM); return; }
	if (player->isDummy || player->GetPlayerUID() == 0) { reject(Protocol::ITEM_RESULT_NOT_ALLOWED); return; }

	// 쓸 행: 지정한 행(인벤토리 위치의 내 것) 또는 그 종류 중 수량이 가장 적은 스택(같으면 ItemDbId가 작은 것 — 덜 찬 스택부터 비운다).
	const ItemInstance* target = nullptr;
	if (itemDbId != 0)
	{
		target = player->inventory.Find(itemDbId);
		if (target != nullptr && target->equipPart != 0) target = nullptr;
		// 거래에 올린 행은 쓸 수 없다.
		if (target != nullptr && IsItemInTrade(player, itemDbId)) { reject(Protocol::ITEM_RESULT_IN_TRADE); return; }
	}
	else if (itemId != 0)
	{
		for (const auto& [dbId, item] : player->inventory.Items())
		{
			if (item.equipPart != 0 || item.itemId != itemId) continue;
			if (IsItemInTrade(player, dbId)) continue;   // 거래에 올린 스택은 건너뛴다
			if (target == nullptr || item.count < target->count) target = &item;
		}
	}
	if (target == nullptr || target->count < 1) { reject(Protocol::ITEM_RESULT_NO_ITEM); return; }

	const ItemData* item = DataManager::GetItemData(target->itemId);
	if (item == nullptr) { reject(Protocol::ITEM_RESULT_NO_ITEM); return; }
	if (item->useEffect == ItemUseEffect::None) { reject(Protocol::ITEM_RESULT_NOT_USABLE); return; }
	if (item->requiredLevel > player->GetInfo().level()) { reject(Protocol::ITEM_RESULT_LEVEL_TOO_LOW); return; }
	if (player->GetStat().IsDead()) { reject(Protocol::ITEM_RESULT_DEAD); return; }
	// 회복할 것이 없으면 쓰지 않는다(아이템을 버리지 않게).
	if (item->useEffect == ItemUseEffect::HealHp && player->GetInfo().stat().hp() >= player->GetInfo().stat().max_hp()) { reject(Protocol::ITEM_RESULT_HP_FULL); return; }

	// 재사용 시간: 종류별 다음 사용 가능 시각과 비교한다(타이머·저장 없음).
	const long long nowMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	const auto cooldownIt = player->itemCooldownUntilMs.find(item->itemId);
	if (cooldownIt != player->itemCooldownUntilMs.end() && nowMs < cooldownIt->second) { reject(Protocol::ITEM_RESULT_COOLDOWN); return; }

	if (!player->itemOp.interval.IsReady()) { reject(Protocol::ITEM_RESULT_TOO_FREQUENT); return; }
	if (player->itemOp.inFlight) { Metrics::ItemOpBusy.fetch_add(1); reject(Protocol::ITEM_RESULT_BUSY); return; }

	DB_ItemOp op;
	op.kind = ItemOpKind::Use;
	op.itemId = item->itemId;
	op.useDbId = target->dbId;
	op.useExpectCount = target->count;

	player->itemOp.interval.Reset(DataManager::GetEconomy().itemMinIntervalMs);
	StartItemOp(player, op);
}

void Room::PlayerSetQuickSlot(shared_ptr<Player> player, int slot, int itemId)
{
	if (player == nullptr) return;

	auto reject = [&](Protocol::ItemResult result) {
		LOG_DEBUG("Item", "액티브 칸 거부 player={} slot={} item={} result={}", player->GetId(), slot, itemId, Protocol::ItemResult_Name(result));
		if (shared_ptr<Session> session = player->GetSession())
			session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_SET_QUICKSLOT, result));
	};

	if (false == IsInRoom(player)) { reject(Protocol::ITEM_RESULT_NOT_IN_ROOM); return; }
	if (player->isDummy || player->GetPlayerUID() == 0) { reject(Protocol::ITEM_RESULT_NOT_ALLOWED); return; }
	if (!player->itemOp.interval.IsReady()) { reject(Protocol::ITEM_RESULT_TOO_FREQUENT); return; }
	if (player->roomMove.Active()) { reject(Protocol::ITEM_RESULT_BUSY); return; }   // Room 이동 대기·이동 중(DG3b)
	if (slot < 1 || slot > QUICK_SLOT_COUNT) { reject(Protocol::ITEM_RESULT_BAD_SLOT); return; }

	if (itemId != 0)
	{
		// 칸에는 사용할 수 있는 종류만 올린다(보유 여부는 보지 않는다 — 다 써도 칸은 남고 수량 0으로 보인다).
		const ItemData* item = DataManager::GetItemData(itemId);
		if (item == nullptr) { reject(Protocol::ITEM_RESULT_NO_ITEM); return; }
		if (item->useEffect == ItemUseEffect::None) { reject(Protocol::ITEM_RESULT_NOT_USABLE); return; }
		// 지금 무기의 스킬이 있는 칸은 스킬이 우선이라 올릴 수 없다(D-154).
		if (DataManager::GetSkillBySlot(player->GetWeaponType(), slot) != nullptr) { reject(Protocol::ITEM_RESULT_BAD_SLOT); return; }
	}

	QuickSlotArray& slots = player->quickSlots;
	if (slots[slot - 1] == itemId) return;   // 바뀌는 것이 없다

	// 한 종류는 한 칸에만 둔다: 다른 칸에 있던 것은 비운다(옮기기).
	if (itemId != 0)
	{
		for (int32_t& other : slots)
			if (other == itemId) other = 0;
	}
	slots[slot - 1] = itemId;
	player->itemOp.interval.Reset(DataManager::GetEconomy().itemMinIntervalMs);

	// 소유가 바뀌지 않는 설정값이라 메모리에 먼저 반영하고 알린 뒤 저장한다(itemOp를 쓰지 않는다).
	if (shared_ptr<Session> session = player->GetSession())
		session->Send(PacketSerializer::MAKE_SC_INVENTORY_UPDATE({}, {}, player->GetGold(), &slots));
	RequestQuickSlotSave(player);
}

void Room::RequestQuickSlotSave(shared_ptr<Player> player)
{
	if (player == nullptr || player->GetPlayerUID() == 0 || player->isDummy) return;

	// Room 이동 대기·이동 중에는 새 저장을 보내지 않는다(DG3b). 도착 Room이 들어오기 처리 끝에서 다시 부른다(값이 다르면 그때 저장).
	if (player->roomMove.Active() && IsInRoom(player)) return;

	Player::SaveState& state = player->quickSlotSave;
	if (state.inFlight)
	{
		// DB 작업 스레드가 여러 개라 연달아 보내면 순서가 뒤바뀔 수 있다. 끝난 뒤 최신 값으로 한 번 더 보낸다.
		state.dirty = true;
		return;
	}

	state.dirty = false;
	if (player->quickSlots == player->lastSavedQuickSlots) return;   // DB에 있는 값과 같다

	state.inFlight = true;
	GDBWorker->PushDBJob(&DatabaseWorker::SaveQuickSlots, shared_from_this(), player, player->GetPlayerUID(), player->quickSlots);
}

void Room::OnQuickSlotsSaved(shared_ptr<Player> player, QuickSlotArray saved, bool success)
{
	CheckDbResultOwner(player, "quickSlotSave");
	Player::SaveState& state = player->quickSlotSave;
	state.inFlight = false;
	const bool inRoom = IsInRoom(player);

	if (success)
	{
		player->lastSavedQuickSlots = saved;
		// 저장하는 동안 또 바뀌었거나 그 사이 방을 나갔으면 최신 값으로 한 번 더(값이 같으면 보내지 않는다).
		if (state.dirty || !inRoom)
			RequestQuickSlotSave(player);
	}
	// 실패: 방에 있으면 다음 변경·퇴장 때 다시 저장한다(lastSavedQuickSlots가 그대로라 값이 다르면 다시 보낸다).
	// 방을 나갔으면 다음 기회가 없으므로 몇 번 더 시도한다.
	else if (!inRoom)
	{
		constexpr int MAX_LEAVE_SAVE_RETRIES = 3;
		if (player->quickSlotSaveRetries < MAX_LEAVE_SAVE_RETRIES)
		{
			++player->quickSlotSaveRetries;
			RequestQuickSlotSave(player);
		}
		else
		{
			LOG_ERROR("Room", "액티브 칸 퇴장 저장을 포기합니다 (PlayerId {}) — DB에는 이전 배치가 남습니다", player->GetPlayerUID());
		}
	}

	TryAdvanceMove(player);   // 이 저장을 기다리던 Room 이동(DG3b)
	// 방을 나간 플레이어의 마지막 저장이 끝났으면 접속 목록에서 빼게 한다(A2.7).
	NotifyClosedIfDone(player);
}

bool Room::IsInRoom(const shared_ptr<Player>& player) const
{
	if (player == nullptr) return false;
	const auto it = _players.find(player->GetId());
	return it != _players.end() && it->second == player;
}

Protocol::ItemResult Room::CheckItemRequest(const shared_ptr<Player>& player)
{
	if (false == IsInRoom(player)) return Protocol::ITEM_RESULT_NOT_IN_ROOM;
	// 더미 봇은 전원이 DB 한 행을 쓴다 — 아이템·골드 요청을 모두 거부한다.
	if (player->isDummy || player->GetPlayerUID() == 0) return Protocol::ITEM_RESULT_NOT_ALLOWED;
	if (!player->itemOp.interval.IsReady()) return Protocol::ITEM_RESULT_TOO_FREQUENT;
	// Room 이동 대기·이동 중에는 새 DB 작업을 시작하지 않는다(DG3b) — 결과가 옛 Room으로 돌아오면 메모리에 반영되지 않는다.
	if (player->roomMove.Active())
	{
		Metrics::ItemOpBusy.fetch_add(1);
		return Protocol::ITEM_RESULT_BUSY;
	}
	if (player->itemOp.inFlight)
	{
		Metrics::ItemOpBusy.fetch_add(1);
		return Protocol::ITEM_RESULT_BUSY;
	}
	return Protocol::ITEM_RESULT_SUCCESS;
}

void Room::StartItemOp(const shared_ptr<Player>& player, DB_ItemOp op)
{
	op.playerUID = player->GetPlayerUID();
	op.requestUs = Metrics::NowUs();

	// 시험용: N번째 아이템 트랜잭션을 일부러 롤백시킨다([Debug] DbItemFailEvery, D-162)
	static atomic<long long> opCounter{ 0 };
	if (GServerConfig.dbItemFailEvery > 0 && (opCounter.fetch_add(1) + 1) % GServerConfig.dbItemFailEvery == 0)
		op.forceFail = true;

	player->itemOp.inFlight = true;
	player->itemOp.weaponSwapPending = (op.kind == ItemOpKind::Equip && op.equipPart == Protocol::EQUIP_PART_WEAPON);
	Metrics::ItemOpSent.fetch_add(1);
	Metrics::ItemOpInFlight.fetch_add(1);
	// 경매 등록·구매·취소(A4.3)는 경매 서비스가 목록으로 먼저 검증한다(서비스 → DB → 서비스 → OnItemOpDone). 수령은 다른 작업처럼 DB로 바로 간다.
	if (op.kind == ItemOpKind::AuctionRegister || op.kind == ItemOpKind::AuctionBuy || op.kind == ItemOpKind::AuctionCancel)
		GAuction->PushJob(&AuctionService::BeginOp, shared_from_this(), player, op);
	else
		GDBWorker->PushDBJob(&DatabaseWorker::ExecItemOp, shared_from_this(), player, op);
}

void Room::OnItemOpDone(shared_ptr<Player> player, DB_ItemOp op, DB_ItemOpResult result)
{
	CheckDbResultOwner(player, "itemOp");
	ApplyItemOpResult(player, std::move(op), std::move(result));
	// 이 결과를 기다리던 Room 이동이 있으면 이어 간다(DG3b). 거래의 상대 쪽은 OnTradeDone 끝에서.
	TryAdvanceMove(player);
}

void Room::ApplyItemOpResult(shared_ptr<Player> player, DB_ItemOp op, DB_ItemOpResult result)
{
	Metrics::ItemOpInFlight.fetch_sub(1);
	player->itemOp.inFlight = false;
	player->itemOp.weaponSwapPending = false;

	const long long totalUs = Metrics::NowUs() - op.requestUs;
	Metrics::ItemOpTotalUs.Add(totalUs);
	if (!result.success && result.auctionResult == 0)   // 경매의 사유 있는 거부(서비스·DB가 판단, 바뀐 것 없음)는 DB 실패로 세지 않는다
		Metrics::ItemOpFailed.fetch_add(1);

	// 방을 나간 플레이어(접속 해제)는 메모리·패킷 반영을 건너뛴다. DB에는 이미 들어갔고 다음 로그인 때 읽는다.
	const bool inRoom = IsInRoom(player);
	const shared_ptr<GameSession> session = inRoom ? player->GetSession() : nullptr;

	// 결과를 알 수 없는 실패(실행 오류·결과 행 없음 — 예: COMMIT 뒤 연결이 끊김)는 DB에 반영됐을 수도 있다.
	// 메모리를 그대로 두면 DB와 어긋난 채 남으므로 접속을 끊어 다음 로그인 때 DB 값을 다시 읽게 한다.
	// (롤백이 확인된 실패는 메모리 = DB라 끊지 않고 DB_ERROR만 알린다.)
	if (result.unknown)
	{
		LOG_ERROR("Item", "아이템 작업 결과를 알 수 없어 접속을 끊습니다 (PlayerId {}) — 다음 로그인 때 DB 값을 다시 읽습니다", op.playerUID);
		if (inRoom && session) session->ForceClose(L"ItemOp: unknown DB result");
	}

	// 행 하나의 메모리 값을 바꾸고 보낼 목록에 넣는다
	vector<ItemInstance> changed;
	// slot = 인벤토리 칸(장착하면 -1)
	auto setEquipPart = [&](int64_t dbId, int equipPart, int slot) {
		const ItemInstance* found = player->inventory.Find(dbId);
		if (found == nullptr) return;
		ItemInstance moved = *found;
		moved.equipPart = equipPart;
		moved.slot = slot;
		player->inventory.Set(moved);
		changed.push_back(moved);
	};

	// 들어온 아이템·골드(지급·줍기의 계획 fills·newCounts·goldDelta)를 메모리에 반영하고 본인에게 알린다.
	auto applyAdded = [&]() {
		if (result.newDbIds.size() != op.newCounts.size())
		{
			// 일어나면 안 된다(쿼리와 결과 읽기가 어긋남). DB에는 반영됐으므로 다음 로그인 때 맞춰진다.
			LOG_ERROR("Item", "지급·줍기 결과의 새 행 수가 다릅니다 (PlayerId {}, 기대 {}, 받음 {})", op.playerUID, op.newCounts.size(), result.newDbIds.size());
			return;
		}
		for (const DB_ItemOp::Fill& fill : op.fills)
		{
			const ItemInstance* found = player->inventory.Find(fill.itemDbId);
			if (found == nullptr) continue;
			ItemInstance filled = *found;
			filled.count = fill.expectCount + fill.add;
			player->inventory.Set(filled);
			changed.push_back(filled);
		}
		for (size_t i = 0; i < op.newCounts.size(); ++i)
		{
			const ItemInstance created{ result.newDbIds[i], op.itemId, op.newCounts[i], 0, op.newSlots[i] };
			player->inventory.Set(created);
			changed.push_back(created);
		}
		if (result.gold >= 0)
			player->SetGold(result.gold);
		if (session) session->Send(PacketSerializer::MAKE_SC_INVENTORY_UPDATE(changed, {}, player->GetGold()));
	};

	switch (op.kind)
	{
	case ItemOpKind::Give:
	{
		if (!result.success)
		{
			if (session) session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_DEV_GIVE, Protocol::ITEM_RESULT_DB_ERROR));
			break;
		}
		if (!inRoom) break;
		applyAdded();
		break;
	}
	case ItemOpKind::Pickup:
	{
		// 줍는 중인 드랍은 소멸·상한 제거가 건너뛰므로 같은 id로 그대로 남아 있다.
		const shared_ptr<DropItem> drop = GetDrop(op.dropId);
		if (!result.success)
		{
			if (drop != nullptr && drop->picking)
			{
				drop->picking = false;
				const long long nowMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
				if (result.unknown)
				{
					// DB에 들어갔을 수도 있다 — 바닥에 되돌리면 복제가 될 수 있으므로 지운다(플레이어는 위에서 끊었다).
					RemoveDrop(op.dropId);
				}
				else if (nowMs >= drop->expireAtMs)
				{
					// 줍는 동안 수명이 다했다(ExpireDrop이 건너뛴 것).
					RemoveDrop(op.dropId);
					Metrics::DropsExpired.fetch_add(1);
				}
			}
			if (session) session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_PICKUP_ITEM, Protocol::ITEM_RESULT_DB_ERROR));
			break;
		}
		// 월드 효과(드랍 제거)는 플레이어가 방을 나갔어도 적용한다 — DB에는 이미 들어갔다.
		if (RemoveDrop(op.dropId))
			Metrics::DropsPicked.fetch_add(1);
		LOG_DEBUG("Item", "줍기 player={} uid={} drop={} item={} gold={}", player->GetId(), op.playerUID, op.dropId, op.itemId, op.goldDelta);
		if (!inRoom) break;
		applyAdded();
		break;
	}
	case ItemOpKind::ShopBuy:
	{
		if (!result.success)
		{
			if (session) session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_SHOP_BUY, Protocol::ITEM_RESULT_DB_ERROR));
			break;
		}
		LOG_DEBUG("Item", "상점 구매 player={} uid={} item={} gold={} 잔액={}", player->GetId(), op.playerUID, op.itemId, op.goldDelta, result.gold);
		if (!inRoom) break;
		applyAdded();
		break;
	}
	case ItemOpKind::ShopSell:
	{
		if (!result.success)
		{
			if (session) session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_SHOP_SELL, Protocol::ITEM_RESULT_DB_ERROR));
			break;
		}
		LOG_DEBUG("Item", "상점 판매 player={} uid={} dbId={} item={} count={}/{} gold=+{} 잔액={}", player->GetId(), op.playerUID, op.sellDbId, op.itemId, op.sellCount, op.sellExpectCount, op.goldDelta, result.gold);
		if (!inRoom) break;

		// 수량: 전량이면 행이 사라지고, 아니면 판 만큼 줄어든다.
		vector<int64_t> removed;
		if (const ItemInstance* found = player->inventory.Find(op.sellDbId))
		{
			if (op.sellCount >= op.sellExpectCount)
			{
				player->inventory.Remove(op.sellDbId);
				removed.push_back(op.sellDbId);
			}
			else
			{
				ItemInstance left = *found;
				left.count = op.sellExpectCount - op.sellCount;
				player->inventory.Set(left);
				changed.push_back(left);
			}
		}
		if (result.gold >= 0)
			player->SetGold(result.gold);
		if (session) session->Send(PacketSerializer::MAKE_SC_INVENTORY_UPDATE(changed, removed, player->GetGold()));
		break;
	}
	case ItemOpKind::Discard:
	{
		if (!result.success)
		{
			// 결과를 알 수 없을 때도 드랍을 만들지 않는다(DB에 행이 남아 있을 수 있어, 만들면 복제가 된다).
			if (session) session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_DROP_ITEM, Protocol::ITEM_RESULT_DB_ERROR));
			break;
		}
		if (inRoom)
		{
			player->inventory.Remove(op.discardDbId);
			if (session) session->Send(PacketSerializer::MAKE_SC_INVENTORY_UPDATE({}, { op.discardDbId }, player->GetGold()));
		}
		// 월드 효과(드랍 생성)는 방을 나갔어도 적용한다 — DB에서는 이미 지웠다. 방에 있으면 지금 위치, 없으면 요청 때 위치.
		// 발밑 둘레의 갈 수 있는 자리(내비메시 위)에 놓는다. 소유권 없음(누구나 주울 수 있다).
		Protocol::PositionInfo base;
		if (inRoom) base = player->GetPosition();
		else { base.set_x(op.discardX); base.set_y(op.discardY); base.set_z(op.discardZ); }
		const float scatter = static_cast<float>(DataManager::GetEconomy().dropScatterCm);
		const Protocol::PositionInfo dropPos = scatter > 0.f ? _gameMap.GetRandomPosNear(base, scatter) : base;
		CreateDrop(op.itemId, op.discardCount, dropPos, 0);
		Metrics::DropsDiscarded.fetch_add(1);
		break;
	}
	case ItemOpKind::Equip:
	{
		Metrics::EquipApplyUs.Add(totalUs);
		const Protocol::EquipPart part = static_cast<Protocol::EquipPart>(op.equipPart);
		if (!result.success)
		{
			if (session) session->Send(PacketSerializer::MAKE_SC_CHANGE_EQUIP_RESULT(Protocol::EQUIP_RESULT_DB_ERROR, part, op.itemId, op.newDbId));
			break;
		}
		if (!inRoom) break;

		// 여기서는 사망·시전 여부를 다시 보지 않는다 — 이미 DB에 반영됐다(DESIGN_ITEMS_TRADE "장착 검사 확장").
		if (op.newDbId != 0) setEquipPart(op.newDbId, op.equipPart, -1);
		if (op.oldDbId != 0) setEquipPart(op.oldDbId, 0, op.oldSlot);
		if (op.offhandDbId != 0) setEquipPart(op.offhandDbId, 0, op.offhandSlot);

		// 무기가 바뀌면 조준 중인 스킬도 바뀌므로 조준을 해제한다. 상태만 바꾼다 — 원격은 SC_CHANGE_EQUIP으로, 본인은 자기 무기 교체 처리로 끈다.
		if (part == Protocol::EQUIP_PART_WEAPON && player->IsSkillAiming())
			player->SetSkillAim(nullptr, chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count());

		player->SetEquippedItem(part, op.itemId);
		const bool offhandCleared = op.offhandDbId != 0;
		if (offhandCleared)
			player->SetEquippedItem(Protocol::EQUIP_PART_OFFHAND, 0);

		player->RecalcStat();

		// 무기 슬롯이 바뀌면 착용·해제 모두 행동 잠금 — 스왑으로 쿨타임을 우회하지 못하게 한다(DECISIONS 2026-11-04, 2026-11-07).
		// 잠금은 결과를 적용하는 지금부터 건다.
		if (part == Protocol::EQUIP_PART_WEAPON)
		{
			const MovementData* rule = DataManager::GetMovementData(PLAYER_MOVEMENT_ID);
			player->LockActionForSwap(rule->weaponSwapLockMs);
		}

		// 보조손 해제를 먼저 보내 클라이언트가 "활 + 방패" 상태를 거치지 않게 한다. 두 패킷 모두 최종 스탯을 싣는다.
		if (offhandCleared)
			BroadcastAOI(static_pointer_cast<Character>(player), PacketSerializer::MAKE_SC_CHANGE_EQUIP(player->GetId(), Protocol::EQUIP_PART_OFFHAND, 0, player->GetInfo().stat()), true);
		BroadcastAOI(static_pointer_cast<Character>(player), PacketSerializer::MAKE_SC_CHANGE_EQUIP(player->GetId(), part, op.itemId, player->GetInfo().stat()), true);

		if (session) session->Send(PacketSerializer::MAKE_SC_INVENTORY_UPDATE(changed, {}, player->GetGold()));
		break;
	}
	case ItemOpKind::Trade:
	{
		// 두 사람의 결과를 함께 처리한다(이 함수의 player는 요청한 쪽 A).
		OnTradeDone(op, result);
		break;
	}
	case ItemOpKind::Use:
	{
		if (!result.success)
		{
			if (session) session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_USE_ITEM, Protocol::ITEM_RESULT_DB_ERROR));
			break;
		}
		if (!inRoom) break;

		// 수량: 1개였으면 행이 사라지고, 아니면 1 줄어든다.
		vector<int64_t> removed;
		if (const ItemInstance* found = player->inventory.Find(op.useDbId))
		{
			if (op.useExpectCount <= 1)
			{
				player->inventory.Remove(op.useDbId);
				removed.push_back(op.useDbId);
			}
			else
			{
				ItemInstance used = *found;
				used.count = op.useExpectCount - 1;
				player->inventory.Set(used);
				changed.push_back(used);
			}
		}

		// 효과: DB에서는 이미 썼다. 그 사이 죽었으면 회복하지 않는다(OnHealed가 죽은 대상을 건너뛴다 — 부활은 CS_REVIVE로만).
		const ItemData* item = DataManager::GetItemData(op.itemId);
		int cooldownMs = 0;
		if (item != nullptr)
		{
			if (item->useEffect == ItemUseEffect::HealHp)
				player->GetStat().OnHealed(item->useValue);
			cooldownMs = item->useCooldownMs;
			const long long nowMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
			player->itemCooldownUntilMs[op.itemId] = nowMs + cooldownMs;
		}

		BroadcastAOI(static_pointer_cast<Character>(player), PacketSerializer::MAKE_SC_USE_ITEM(player->GetId(), op.itemId, player->GetInfo().stat().hp(), cooldownMs), true);
		if (session) session->Send(PacketSerializer::MAKE_SC_INVENTORY_UPDATE(changed, removed, player->GetGold()));
		break;
	}
	case ItemOpKind::Move:
	{
		if (!result.success)
		{
			if (session) session->Send(PacketSerializer::MAKE_SC_ITEM_RESULT(Protocol::CS_MOVE_ITEM, Protocol::ITEM_RESULT_DB_ERROR));
			break;
		}
		if (!inRoom) break;
		// 장착 여부(0)는 그대로 두고 칸만 바꾼다.
		setEquipPart(op.moveDbId, 0, op.moveTo);
		if (op.moveOtherDbId != 0) setEquipPart(op.moveOtherDbId, 0, op.moveFrom);
		if (session) session->Send(PacketSerializer::MAKE_SC_INVENTORY_UPDATE(changed, {}, player->GetGold()));
		break;
	}
	case ItemOpKind::AuctionRegister:
	case ItemOpKind::AuctionBuy:
	case ItemOpKind::AuctionCancel:
	case ItemOpKind::MailClaim:
	{
		// 요청 핸들러의 TryBeginDbWork와 짝이다. 서비스가 거부해 DB에 가지 않았어도, 플레이어가 방을 나갔어도 여기로 온다.
		AuctionService::EndDbWork();

		Protocol::PacketId request = Protocol::CS_MAIL_CLAIM;
		int64_t targetId = op.mailId;
		if (op.kind == ItemOpKind::AuctionRegister) { request = Protocol::CS_AUCTION_REGISTER; targetId = 0; }
		else if (op.kind == ItemOpKind::AuctionBuy) { request = Protocol::CS_AUCTION_BUY; targetId = op.listingId; }
		else if (op.kind == ItemOpKind::AuctionCancel) { request = Protocol::CS_AUCTION_CANCEL; targetId = op.listingId; }

		if (!result.success)
		{
			// 사유가 있으면 서비스나 DB가 거부한 것(바뀐 것 없음), 없으면 DB 실패다.
			const Protocol::AuctionResult reason = result.auctionResult != 0
				? static_cast<Protocol::AuctionResult>(result.auctionResult) : Protocol::AUCTION_RESULT_DB_ERROR;
			Metrics::AuctionRejected.fetch_add(1);
			LOG_DEBUG("Auction", "실패 player={} uid={} request={} id={} result={}", player->GetId(), op.playerUID, Protocol::PacketId_Name(request), targetId, Protocol::AuctionResult_Name(reason));
			if (session) session->Send(PacketSerializer::MAKE_SC_AUCTION_RESULT(request, reason, targetId));
			break;
		}
		if (op.kind == ItemOpKind::MailClaim) Metrics::AuctionClaimed.fetch_add(1);
		if (!inRoom) break;

		vector<int64_t> removed;
		if (op.kind == ItemOpKind::AuctionRegister)
		{
			// 올린 수량이 인벤토리에서 빠진다: 통째면 행이 사라지고(DB에서는 경매 보관으로 옮겨짐), 일부면 그만큼 줄어든다.
			targetId = result.listingId;
			if (const ItemInstance* found = player->inventory.Find(op.auctionDbId))
			{
				if (op.auctionCount >= op.auctionExpectCount)
				{
					player->inventory.Remove(op.auctionDbId);
					removed.push_back(op.auctionDbId);
				}
				else
				{
					ItemInstance left = *found;
					left.count = op.auctionExpectCount - op.auctionCount;
					player->inventory.Set(left);
					changed.push_back(left);
				}
			}
		}
		else if (op.kind == ItemOpKind::MailClaim && result.claimDbId != 0)
		{
			// 받은 아이템은 기존 스택에 합치지 않고 고른 빈 칸에 한 행으로 들어온다.
			if (DataManager::GetItemData(result.claimItemId) != nullptr)
			{
				const ItemInstance got{ result.claimDbId, result.claimItemId, result.claimCount, 0, op.claimSlot };
				player->inventory.Set(got);
				changed.push_back(got);
			}
			else
			{
				// ItemTable에 없는 종류: 로그인 때처럼 메모리에는 올리지 않고 그 칸만 비워 둔다(DB 행은 그대로).
				LOG_WARN("Item", "수령한 아이템 종류 {} 가 ItemTable에 없습니다 (PlayerId {}, ItemDbId {})", result.claimItemId, op.playerUID, result.claimDbId);
				player->inventory.ReserveSlot(op.claimSlot);
			}
		}
		// 구매는 골드만 줄고(산 물건은 수령함), 취소는 인벤토리가 바뀌지 않는다(아이템은 수령함).
		if (result.gold >= 0)
			player->SetGold(result.gold);

		if (session)
		{
			if (op.kind != ItemOpKind::AuctionCancel)
				session->Send(PacketSerializer::MAKE_SC_INVENTORY_UPDATE(changed, removed, player->GetGold()));
			session->Send(PacketSerializer::MAKE_SC_AUCTION_RESULT(request, Protocol::AUCTION_RESULT_SUCCESS, targetId));
		}
		break;
	}
	case ItemOpKind::LoginFix:
	{
		// 메모리는 로그인 때 이미 내렸다. 실패하면 DB에는 장착으로 남아 다음 로그인 때 다시 시도한다
		// (그 사이 그 행을 장착하려 하면 트랜잭션 조건 불일치로 롤백되어 DB_ERROR — 손실·중복은 없다).
		if (result.success)
			player->pendingEquipFix.clear();
		else
			LOG_ERROR("Item", "로그인 장착 보정 실패 (PlayerId {}, 행 {}개) — 다음 로그인 때 다시 시도합니다", op.playerUID, op.demoteDbIds.size());
		break;
	}
	}

	// 방을 나간 플레이어의 마지막 DB 작업이 끝났으면 접속 목록에서 빼게 한다(A2.7).
	NotifyClosedIfDone(player);
}

void Room::UpdateSaves(long long nowMs)
{
	const long long intervalMs = static_cast<long long>(GServerConfig.playerSaveIntervalSec) * 1000;
	for (const auto& [id, player] : _players)
	{
		if (nowMs < player->nextProgressSaveMs) continue;
		if (player->roomMove.Active()) continue;   // 이동이 끝난 뒤(도착 Room의 다음 틱) 저장한다
		player->nextProgressSaveMs = nowMs + intervalMs;
		RequestProgressSave(player);
	}
	UpdateMoves(nowMs);
}

void Room::RequestProgressSave(shared_ptr<Player> player)
{
	if (player == nullptr || player->GetPlayerUID() == 0) return;   // DB 캐릭터가 아닌 경우(로그인 전 등)
	if (player->isDummy) return;                                     // 더미 봇은 전원이 한 행을 쓴다
	// Room 이동 대기·이동 중에는 새 저장을 보내지 않는다(DG3b). 값은 Player가 들고 가고 도착 Room의 주기 저장이 이어받는다.
	if (player->roomMove.Active() && IsInRoom(player)) return;

	Player::SaveState& state = player->progressSave;
	if (state.inFlight)
	{
		state.dirty = true;
		Metrics::ProgressSaveCoalesced.fetch_add(1);
		return;
	}

	// DB에 있는 값과 같으면 보내지 않는다.
	const DB_PlayerProgress snapshot = player->MakeProgressSnapshot();
	state.dirty = false;
	if (snapshot == player->lastSavedProgress) return;

	state.inFlight = true;
	Metrics::ProgressSaveSent.fetch_add(1);
	Metrics::ProgressSaveInFlight.fetch_add(1);
	GDBWorker->PushDBJob(&DatabaseWorker::SavePlayerProgress, shared_from_this(), player, player->GetPlayerUID(), snapshot, Metrics::NowUs());
}

void Room::OnProgressSaved(shared_ptr<Player> player, DB_PlayerProgress saved, bool success)
{
	CheckDbResultOwner(player, "progressSave");
	Metrics::ProgressSaveInFlight.fetch_sub(1);
	Player::SaveState& state = player->progressSave;
	state.inFlight = false;

	// 같은 id를 다른 플레이어가 다시 쓸 수 있으므로 포인터로 비교한다.
	const auto it = _players.find(player->GetId());
	const bool inRoom = it != _players.end() && it->second == player;

	if (success)
	{
		player->lastSavedProgress = saved;
		// 저장하는 동안 다시 요청됐거나 그 사이 방을 나갔으면 최신 값으로 한 번 더 저장한다(값이 같으면 보내지 않는다).
		if (state.dirty || !inRoom)
			RequestProgressSave(player);
	}
	// 실패: 방에 있으면 다음 주기에 다시 저장한다(lastSavedProgress가 그대로라 값이 다르면 다시 보낸다).
	// 방을 나갔으면 다음 기회가 없으므로 몇 번 더 시도하고, 그래도 안 되면 값을 로그에 남긴다.
	else if (!inRoom)
	{
		constexpr int MAX_LEAVE_SAVE_RETRIES = 3;
		if (player->progressSaveRetries < MAX_LEAVE_SAVE_RETRIES)
		{
			++player->progressSaveRetries;
			RequestProgressSave(player);
		}
		else
		{
			const DB_PlayerProgress lost = player->MakeProgressSnapshot();
			LOG_ERROR("Room", "퇴장 저장을 포기합니다 (PlayerId {}, Level {}, Exp {}, Hp {}) — DB에는 이전 값이 남습니다",
				player->GetPlayerUID(), lost.level, lost.exp, lost.hp);
		}
	}

	TryAdvanceMove(player);   // 이 저장을 기다리던 Room 이동(DG3b)
	// 방을 나간 플레이어의 마지막 저장이 끝났으면(다시 보낸 저장이 없으면) 접속 목록에서 빼게 한다.
	NotifyClosedIfDone(player);
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

void Room::CastSkill(shared_ptr<Character> caster, int slot, int targetId, float aimYaw, float aimX, float aimY, uint32_t seq)
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

	// 거부(CP1, D-211): 플레이어 본인에게만 사유와 요청 순번을 돌려준다. 클라이언트는 입력 때 먼저 시작한 시전을 되돌린다.
	auto reject = [&](Protocol::AttackResult result, long long remainMs = 0) {
		if (!casterIsPlayer) return;
		Metrics::AttackRejected.fetch_add(1);
		LOG_DEBUG("Attack", "거부 player={} slot={} seq={} result={} remain={}ms", caster->GetId(), slot, seq, Protocol::AttackResult_Name(result), remainMs);
		if (shared_ptr<Session> session = static_pointer_cast<Player>(caster)->GetSession())
			session->Send(PacketSerializer::MAKE_SC_ATTACK_RESULT(result, seq, slot, static_cast<int>(remainMs)));
	};

	// Room 이동 표시 중에는 시전을 받지 않는다(DG3b) — 클라이언트는 곧 오는 SC_ROOM_CHANGE에서 먼저 시작한 시전을 버린다.
	if (casterIsPlayer && static_pointer_cast<Player>(caster)->roomMove.transfer) return reject(Protocol::ATTACK_RESULT_ACTING);

	// 무기 교체가 DB에 가 있는 동안에는 시전을 받지 않는다(A3.3). 결과가 오면 스왑 잠금이 걸리고 스킬 구성도 바뀐다.
	if (casterIsPlayer && static_pointer_cast<Player>(caster)->itemOp.weaponSwapPending) return reject(Protocol::ATTACK_RESULT_SWAP_PENDING);

	// 허용 오차(D-213)는 플레이어만: 지연이 흔들려 앞 동작·쿨타임이 끝나기 조금 전에 도착한 요청을 받아 준다(기본 0 = 끔).
	Character::CastCheck check;
	const SkillData* skill = caster->FindCastableSkill(slot, &check, casterIsPlayer ? GServerConfig.attackEarlyToleranceMs : 0);
	if (skill == nullptr) return reject(check.reason, check.remainMs);

	const Protocol::PositionInfo& pos = caster->GetPosition();
	constexpr float RAD_TO_DEG = 180.0f / MathUtils::PI;

	float yaw = aimYaw;
	float ax = aimX;
	float ay = aimY;

	if (skill->targeting == SkillTargeting::Targeted)
	{
		// 클라이언트가 보낸 대상 id는 신뢰하지 않고, 존재/생존/적대 관계/사거리를 서버에서 다시 확인한다.
		shared_ptr<GameObject> targetObj = GetGameObject(targetId);
		if (targetObj == nullptr) return reject(Protocol::ATTACK_RESULT_NO_TARGET);

		const Protocol::ObjectType wantedType = casterIsPlayer ? Protocol::ObjectType::OBJECT_TYPE_MONSTER : Protocol::ObjectType::OBJECT_TYPE_PLAYER;
		if (targetObj->GetType() != wantedType) return reject(Protocol::ATTACK_RESULT_NO_TARGET);
		if (static_pointer_cast<Character>(targetObj)->GetStat().IsDead()) return reject(Protocol::ATTACK_RESULT_TARGET_DEAD);

		const float dx = targetObj->GetPosition().x() - pos.x();
		const float dy = targetObj->GetPosition().y() - pos.y();
		const float dist = std::sqrt(dx * dx + dy * dy);
		// 이동 지연을 감안한 10% 여유 + 대상의 몸 반경(MN1, D-198)
		if (dist > skill->castRange * 1.1f + static_pointer_cast<Character>(targetObj)->GetBodyRadius()) return reject(Protocol::ATTACK_RESULT_TOO_FAR);

		yaw = std::atan2(dy, dx) * RAD_TO_DEG;
	}
	else
	{
		if (!std::isfinite(yaw)) return reject(Protocol::ATTACK_RESULT_BAD_REQUEST);

		if (skill->hitShape == HitShape::Zone)
		{
			if (!std::isfinite(ax) || !std::isfinite(ay)) return reject(Protocol::ATTACK_RESULT_BAD_REQUEST);

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
	const Character::CastTiming timing = caster->BeginCast(*skill, check.actionCarryMs, check.cooldownCarryMs);
	if (casterIsPlayer)
	{
		Metrics::AttackAccepted.fetch_add(1);
		if (check.actionCarryMs > 0 || check.cooldownCarryMs > 0) Metrics::AttackEarlyAccepted.fetch_add(1);
		// 이동 잠금(D-199): 휘두르기가 끝날 때까지 이동을 받지 않는다. 판정 원점(판정 시점의 시전자 위치)이 시전한 자리에 머문다.
		if (const MovementData* rule = DataManager::GetMovementData(PLAYER_MOVEMENT_ID))
			static_pointer_cast<Player>(caster)->BeginMoveLock(chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count(), timing.moveLockMs, *rule);
	}

	BroadcastAOI(caster, PacketSerializer::MAKE_SC_ATTACK(caster->GetId(), skill->skillId, targetId, yaw, ax, ay, static_cast<int>(timing.cooldownMs), seq), true);

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
	if (_closed.load()) return;
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
				// 대상의 몸 반경만큼 장판을 넓게 본다(MN1, D-198). 플레이어는 0이라 radiusSq 그대로.
				const float body = target->GetBodyRadius();
				const float reachSq = body > 0.f ? (zone.radius + body) * (zone.radius + body) : radiusSq;
				if (dx * dx + dy * dy > reachSq) continue;

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

void Room::SpawnMonster(int spawnIndex, int templateId, int respawnMs)
{
	if (_closed.load()) return;
	const auto& spawns = _gameMap.GetMonsterSpawns();
	if (spawnIndex < 0 || spawnIndex >= static_cast<int>(spawns.size())) return;

	const MonsterSpawnPoint& spawn = spawns[spawnIndex];

	// 구역 이름이 없는 마커의 monsterTemplateId가 MonsterTable에 없으면 스탯 없는 몬스터가 생기므로 스폰하지 않는다(SpawnTable 행은 로더가 확인했다).
	// (방은 플레이어가 늘면 실행 중에도 만들어지므로 서버를 멈추지 않고 ERROR만 남긴다)
	if (DataManager::GetMonsterData(templateId) == nullptr)
	{
		LOG_ERROR("Room", "스폰 마커 {} 의 monsterTemplateId {} 가 MonsterTable에 없어 스폰하지 않습니다", spawnIndex, templateId);
		return;
	}

	shared_ptr<Monster> monster = make_shared<Monster>(templateId);
	monster->SetId(MonsterIdGenerator());
	monster->SetSpawnInfo(spawnIndex, respawnMs);
	// 던전의 우두머리(구역 이름 boss — D-225): 전부 죽으면 클리어(OnBossDead)
	if (_info.kind == RoomKind::Dungeon && spawn.ZoneName == ZONE_BOSS)
	{
		monster->SetBoss(true);
		++_bossAlive;
	}

	Protocol::PositionInfo spawnPos;
	spawnPos.set_x(spawn.Pos.X);
	spawnPos.set_y(spawn.Pos.Y);
	spawnPos.set_z(spawn.Pos.Z);
	spawnPos.set_yaw(spawn.Pos.Yaw);

	// 마커 반경 안의 임의 위치(L3: 여러 번 시도). 걷는 영역 위로 보정되고(바위·나무 자리는 보정이 실패하거나 가장자리로 밀림),
	// 보정 뒤에도 반경 안이며, 안전 구역 밖인 점만 받는다. 끝내 못 찾으면 마커 위치를 보정해 쓴다.
	NavmeshManager* nav = GetNavManager();
	bool found = false;
	if (spawn.SpawnRadius > 0.f && nav != nullptr)
	{
		for (int i = 0; i < 10 && !found; ++i)
		{
			const float angle = Utils::GetRandom<float>(0.f, 6.2831853f);
			const float dist = spawn.SpawnRadius * sqrtf(Utils::GetRandom<float>(0.f, 1.f));

			Protocol::PositionInfo candidate = spawnPos;
			candidate.set_x(spawn.Pos.X + cosf(angle) * dist);
			candidate.set_y(spawn.Pos.Y + sinf(angle) * dist);

			if (!nav->AdjustPositionToNavMesh(candidate)) continue;
			const float dx = candidate.x() - spawn.Pos.X, dy = candidate.y() - spawn.Pos.Y;
			if (dx * dx + dy * dy > spawn.SpawnRadius * spawn.SpawnRadius) continue;
			if (_gameMap.IsInSafeZone(candidate)) continue;

			spawnPos = candidate;
			found = true;
		}
	}
	if (!found && nav != nullptr)
		nav->AdjustPositionToNavMesh(spawnPos);

	monster->SetPosition(spawnPos);
	monster->SetHome(spawnPos);
	NpcEnterRoom(monster);
}

void Room::RemoveMonster(int monsterId)
{
	shared_ptr<Monster> monster = Id2Monster(monsterId);
	if (monster == nullptr) return;

	const int spawnIndex = monster->GetSpawnIndex();
	const int templateId = monster->GetInfo().template_id();
	const int spawnRespawnMs = monster->GetRespawnMs();   // SpawnTable.RespawnSec (0 = MonsterTable 값)

	// 시야에 있던 플레이어들에게 SC_REMOVE_OBJECT가 전달된다.
	ViewUpdate result = _gameMap.LeaveMap(monsterId, monster->_viewList);
	if (!result.leaved.empty()) {
		UpdateView(monster, result);
	}

	RemoveObject(monsterId);
	_monsters.erase(monsterId);
	Metrics::Monsters.fetch_sub(1);

	// SpawnTable.RespawnSec -1 = 다시 나오지 않는다(던전, DG3c)
	if (spawnRespawnMs < 0) return;

	// 사망 시점부터 RespawnSec가 지난 뒤 같은 마커에서 같은 종류로 새로 만든다. (이미 deathRemoveDelay만큼 지났으므로 그만큼 뺀다)
	const MonsterData* data = DataManager::GetMonsterData(templateId);
	const int respawnMs = spawnRespawnMs > 0 ? spawnRespawnMs : (data ? data->respawnMs : 10000);
	const int delayMs = max(0, respawnMs - (data ? data->deathRemoveDelayMs : 3000));
	ReserveJob(static_cast<DWORD>(delayMs), &Room::SpawnMonster, spawnIndex, templateId, spawnRespawnMs);
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

// ---- 바닥 드랍(A3.4, D-157) ----

int Room::NextDropId()
{
	// DROP_ID_BEGIN부터 올라가다 int32 끝에 닿으면 처음으로 돌아간다. 쓰는 중인 id는 건너뛴다.
	for (;;)
	{
		if (_nextDropId < DROP_ID_BEGIN || _nextDropId == INT_MAX) _nextDropId = DROP_ID_BEGIN;
		else ++_nextDropId;
		if (_drops.find(_nextDropId) == _drops.end()) return _nextDropId;
	}
}

void Room::SpawnMonsterDrops(int monsterTemplateId, Protocol::PositionInfo pos, shared_ptr<Character> attacker)
{
	Metrics::MonsterKills.fetch_add(1);

	vector<DropResult> rolled;
	DataManager::RollDrops(monsterTemplateId, rolled);
	if (rolled.empty()) return;

	// 소유권 = 막타 플레이어. 공격자를 알 수 없거나(투사체가 날아가는 동안 퇴장) 더미 봇(전원이 DB 한 행)이면 소유권 없이 만든다.
	// 막타가 파티 중이면 파티 번호도 적는다(DG2 — 줍는 순간 같은 파티면 주울 수 있다). 그 사이 다른 Room으로 옮긴 공격자의 필드는 읽지 않는다.
	int64_t ownerUID = 0;
	int ownerPartyId = 0;
	if (attacker != nullptr && attacker->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER)
	{
		const shared_ptr<Player> killer = static_pointer_cast<Player>(attacker);
		if (!killer->isDummy)
		{
			ownerUID = killer->GetPlayerUID();
			if (IsInRoom(killer)) ownerPartyId = killer->partyId;
		}
	}

	const float scatter = static_cast<float>(DataManager::GetEconomy().dropScatterCm);
	for (const DropResult& r : rolled)
	{
		// 사망 지점 둘레의 갈 수 있는 자리(내비메시 위). 못 찾으면 사망 지점 그대로.
		const Protocol::PositionInfo dropPos = scatter > 0.f ? _gameMap.GetRandomPosNear(pos, scatter) : pos;
		CreateDrop(r.itemId, r.count, dropPos, ownerUID, ownerPartyId);
	}
}

shared_ptr<DropItem> Room::CreateDrop(int itemId, int64_t count, const Protocol::PositionInfo& pos, int64_t ownerUID, int ownerPartyId)
{
	// 닫힌 방에는 만들지 않는다(접속이 끊긴 사람의 버리기 DB 결과가 닫힌 뒤에 올 수 있다 — DB에서는 이미 지워졌다).
	if (_closed.load()) return nullptr;
	const EconomyData& economy = DataManager::GetEconomy();
	const long long nowMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();

	// 방의 드랍 상한: 가장 오래된 것부터 지운다. 줍는 중인 것은 건너뛴다(DB 결과가 그 드랍을 다시 찾는다).
	while (!_dropOrder.empty() && _drops.find(_dropOrder.front()) == _drops.end())
		_dropOrder.pop_front();
	for (auto it = _dropOrder.begin(); it != _dropOrder.end() && static_cast<int>(_drops.size()) >= economy.maxDropsPerRoom;)
	{
		const shared_ptr<DropItem> old = GetDrop(*it);
		if (old != nullptr && old->picking) { ++it; continue; }
		if (old != nullptr)
		{
			RemoveDrop(*it);
			Metrics::DropsEvicted.fetch_add(1);
		}
		it = _dropOrder.erase(it);
	}

	const shared_ptr<DropItem> drop = make_shared<DropItem>(NextDropId(), itemId, count, pos);
	drop->SetOwnerRoom(shared_from_this());
	drop->ownerUID = ownerUID;
	drop->ownerPartyId = ownerUID != 0 ? ownerPartyId : 0;
	drop->ownerUntilMs = ownerUID != 0 ? nowMs + static_cast<long long>(economy.dropOwnerSec) * 1000 : 0;
	drop->expireAtMs = nowMs + static_cast<long long>(economy.dropLifeSec) * 1000;

	const int dropId = drop->GetId();
	_drops.emplace(dropId, drop);
	_dropOrder.push_back(dropId);
	_gameMap.GetDropAoi().Insert(dropId, pos.x(), pos.y());
	Metrics::Drops.fetch_add(1);
	Metrics::DropsCreated.fetch_add(1);
	LOG_DEBUG("Drop", "생성 id={} item={} count={} owner={} pos=({:.0f},{:.0f},{:.0f})", dropId, itemId, count, ownerUID, pos.x(), pos.y(), pos.z());

	// 반경 R 안의 플레이어에게 보인다(캐릭터 색인에는 몬스터도 있으므로 플레이어만 거른다).
	vector<int> nearby;
	_gameMap.GetAoi().QueryCircle(pos.x(), pos.y(), _gameMap.GetViewRadius(), nearby);
	shared_ptr<SendBuffer> addBuffer;
	for (int id : nearby)
	{
		const shared_ptr<Player> viewer = Id2Player(id);
		if (viewer == nullptr || !viewer->dropView.insert(dropId).second) continue;
		if (addBuffer == nullptr) addBuffer = PacketSerializer::MAKE_SC_ADD_OBJECT(drop);
		if (shared_ptr<Session> session = viewer->GetSession())
			session->Send(addBuffer);
	}

	// 소멸 예약. 예약은 취소할 수 없으므로 실행 때 id로 다시 찾아 확인한다(ExpireDrop).
	ReserveJob(static_cast<DWORD>(economy.dropLifeSec) * 1000, &Room::ExpireDrop, dropId);
	return drop;
}

bool Room::RemoveDrop(int dropId)
{
	const auto it = _drops.find(dropId);
	if (it == _drops.end()) return false;

	const Protocol::PositionInfo pos = it->second->GetPosition();
	_drops.erase(it);
	_gameMap.GetDropAoi().Remove(dropId);
	Metrics::Drops.fetch_sub(1);

	// 보고 있던 플레이어 = R+H 안에서 드랍 시야에 이 id가 있는 사람. 시야는 플레이어가 움직일 때만 갱신되므로
	// 그 뒤로 위치가 바뀌지 않은 사람은 모두 이 범위 안에 있다(경계 오차를 덮으려고 1m 넓게 찾는다).
	vector<int> nearby;
	_gameMap.GetAoi().QueryCircle(pos.x(), pos.y(), _gameMap.GetViewRadius() + _gameMap.GetViewHysteresis() + 100.f, nearby);
	shared_ptr<SendBuffer> removeBuffer;
	for (int id : nearby)
	{
		const shared_ptr<Player> viewer = Id2Player(id);
		if (viewer == nullptr || viewer->dropView.erase(dropId) == 0) continue;
		if (removeBuffer == nullptr) removeBuffer = PacketSerializer::MAKE_SC_REMOVE_OBJECT(dropId);
		if (shared_ptr<Session> session = viewer->GetSession())
			session->Send(removeBuffer);
	}
	return true;
}

void Room::ExpireDrop(int dropId)
{
	const shared_ptr<DropItem> drop = GetDrop(dropId);
	if (drop == nullptr) return;   // 이미 주웠거나 상한으로 지워졌다

	// 같은 id로 나중에 만든 드랍이면 그 드랍의 예약이 따로 있다(타이머 오차 0.5초는 허용).
	const long long nowMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	if (nowMs + 500 < drop->expireAtMs) return;
	// 줍는 중이면 DB 결과 처리가 수명을 다시 본다(A3.5).
	if (drop->picking) return;

	RemoveDrop(dropId);
	Metrics::DropsExpired.fetch_add(1);
}

void Room::UpdateDropView(const shared_ptr<Player>& player)
{
	// 방에 드랍이 없고 보고 있는 것도 없으면 할 일이 없다(대부분의 이동 패킷).
	if (_drops.empty() && player->dropView.empty()) return;

	shared_ptr<Session> session = player->GetSession();
	const Protocol::PositionInfo& pos = player->GetPosition();
	const AoiIndex& dropAoi = _gameMap.GetDropAoi();

	// 나감: 보고 있는데 R+H 밖으로 멀어진 것(R ~ R+H 사이는 그대로 둔다 — 경계 깜빡임 방지). 사라진 드랍은 RemoveDrop이 이미 뺐다.
	const float rOut = _gameMap.GetViewRadius() + _gameMap.GetViewHysteresis();
	for (auto it = player->dropView.begin(); it != player->dropView.end();)
	{
		float x = 0.f, y = 0.f;
		bool gone = !dropAoi.TryGetPos(*it, x, y);
		if (!gone)
		{
			const float dx = x - pos.x(), dy = y - pos.y();
			gone = dx * dx + dy * dy > rOut * rOut;
		}
		if (!gone) { ++it; continue; }
		if (session) session->Send(PacketSerializer::MAKE_SC_REMOVE_OBJECT(*it));
		it = player->dropView.erase(it);
	}

	// 들어옴: 반경 R 안인데 아직 보고 있지 않은 것
	if (_drops.empty()) return;
	vector<int> nearby;
	dropAoi.QueryCircle(pos.x(), pos.y(), _gameMap.GetViewRadius(), nearby);
	for (int dropId : nearby)
	{
		if (!player->dropView.insert(dropId).second) continue;
		const shared_ptr<DropItem> drop = GetDrop(dropId);
		if (drop == nullptr) { player->dropView.erase(dropId); continue; }
		if (session) session->Send(PacketSerializer::MAKE_SC_ADD_OBJECT(drop));
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

void RoomManager::Add(shared_ptr<Room> room)
{
	RWLock::WriteGuard lock(_lock);
	_rooms.insert({ room->GetRoomId(), room });
}

void RoomManager::Remove(int roomId)
{
	shared_ptr<Room> keep;   // 락 밖에서 놓는다(마지막 참조면 여기서 Room이 소멸한다)
	{
		RWLock::WriteGuard lock(_lock);
		const auto it = _rooms.find(roomId);
		if (it == _rooms.end()) return;
		keep = std::move(it->second);
		_rooms.erase(it);
	}
}
