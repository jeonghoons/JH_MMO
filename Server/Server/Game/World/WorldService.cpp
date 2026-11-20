#include "pch.h"
#include "Game/World/WorldService.h"
#include "Game/Room/Room.h"
#include "Game/Object/Player.h"
#include "Game/Lobby/AuthLobby.h"
#include "Utils/Metrics.h"

WorldService::WorldService(HANDLE iocpHandle) : _jobQueue(make_shared<JobQueue>(iocpHandle)), _iocpHandle(iocpHandle)
{
}

WorldService::~WorldService() = default;

bool WorldService::Init()
{
	// 레벨의 마커에 구역 이름(town·field)이 있으면 채널마다 마을·사냥터 Room을 따로 만든다(DG3b). 마커만 한 번 읽어 본다.
	{
		GameMap probe;
		probe.LoadMapData(GServerConfig.mapName);
		_levelSplit = probe.HasArea(AREA_TOWN) && probe.HasArea(AREA_FIELD);
		LOG_INFO("World", "레벨 {}: {}", GServerConfig.mapName, _levelSplit ? "마을·사냥터 구역으로 나눔(채널마다 Room 2개)" : "구역 없음(채널마다 Room 1개)");
	}

	if (CreateChannel(true) == nullptr)
		return false;
	PublishMetrics();
	return true;
}

WorldService::Channel* WorldService::CreateChannel(bool allowBroken)
{
	const int channelId = static_cast<int>(_channels.size()) + 1;

	auto makeRoom = [&](RoomKind kind, const char* area) -> shared_ptr<Room>
	{
		RoomInfo info;
		info.roomId = ++_nextRoomId;
		info.kind = kind;
		info.channel = channelId;
		info.area = area;
		info.levelName = GServerConfig.mapName;   // 로그인한 플레이어가 처음 들어가는 레벨
		shared_ptr<Room> room = make_shared<Room>(GTimer, _iocpHandle, info);
		if (false == room->InitRoom(allowBroken))
			return nullptr;
		return room;
	};

	const long long startUs = Metrics::NowUs();
	// 구역으로 나뉘지 않은 레벨은 Room 하나가 레벨 전체를 맡는다(구역 이름을 비움).
	const shared_ptr<Room> town = makeRoom(RoomKind::Town, _levelSplit ? AREA_TOWN : "");
	if (town == nullptr)
		return nullptr;
	shared_ptr<Room> field;
	if (_levelSplit)
	{
		field = makeRoom(RoomKind::Field, AREA_FIELD);
		if (field == nullptr)
		{
			// 같은 맵 파일을 방금 읽었으므로 여기서만 실패하는 일은 없어야 한다. 먼저 만든 마을 Room은 등록되지 않은 채 남는다(틱만 돈다).
			LOG_ERROR("World", "채널 {} 의 사냥터 Room을 만들지 못했습니다", channelId);
			return nullptr;
		}
	}

	GRoomManager->Add(town);
	if (field != nullptr)
		GRoomManager->Add(field);

	Channel channel;
	channel.id = channelId;
	channel.town = town;
	channel.field = field;
	_channels.push_back(std::move(channel));
	LOG_INFO("World", "채널 {} 생성 (레벨 {}, 마을 Room {}, 사냥터 Room {}, {:.1f}ms)", channelId, GServerConfig.mapName, town->GetRoomId(),
		field != nullptr ? field->GetRoomId() : 0, (Metrics::NowUs() - startUs) / 1000.0);
	return &_channels.back();
}

WorldService::Channel* WorldService::PickChannel()
{
	const WorldData* world = DataManager::GetWorldData(GServerConfig.mapName);   // 서버 시작 때 존재를 확인했다
	const int capacity = world ? world->channelCapacity : 500;

	for (Channel& channel : _channels)
	{
		if (channel.population < capacity)
			return &channel;
	}

	if (false == _channelCreateFailed)
	{
		if (Channel* created = CreateChannel(false))
			return created;
		_channelCreateFailed = true;
		LOG_ERROR("World", "채널 {} 을 만들지 못했습니다. 이후 로그인은 인원이 가장 적은 기존 채널에 정원({})을 넘겨 넣습니다", _channels.size() + 1, capacity);
	}

	Channel* least = nullptr;
	for (Channel& channel : _channels)
	{
		if (least == nullptr || channel.population < least->population)
			least = &channel;
	}
	return least;
}

void WorldService::RegisterPlayer(shared_ptr<Player> player)
{
	if (player == nullptr) return;

	const int sessionId = player->GetId();
	if (_roster.count(sessionId) > 0)
	{
		LOG_ERROR("World", "RegisterPlayer: 세션 {} 이 이미 명부에 있습니다", sessionId);
		return;
	}

	Channel* channel = PickChannel();
	if (channel == nullptr)
	{
		// Init이 성공했다면 채널 1이 있으므로 오지 않는다.
		LOG_ERROR("World", "RegisterPlayer: 배정할 채널이 없습니다 (세션 {})", sessionId);
		return;
	}

	RosterEntry entry;
	entry.playerUID = player->GetPlayerUID();
	entry.name = player->GetName();
	entry.session = player->GetSession();
	entry.channel = channel->id;
	entry.isDummy = player->isDummy;

	++channel->population;
	player->channel = channel->id;

	if (false == entry.isDummy && entry.playerUID != 0)
	{
		_uidIndex[entry.playerUID] = sessionId;
		_nameIndex[entry.name] = sessionId;
	}
	_roster.emplace(sessionId, std::move(entry));
	PublishMetrics();

	LOG_DEBUG("World", "세션 {} 등록: 채널 {} (인원 {})", sessionId, channel->id, channel->population);
}

void WorldService::EnterWorld(shared_ptr<Player> player)
{
	if (player == nullptr) return;

	const auto it = _roster.find(player->GetId());
	if (it == _roster.end())
	{
		// 이미 끊겨 정리된 세션의 입장 요청이 뒤늦게 실행됐다.
		LOG_DEBUG("World", "EnterWorld: 세션 {} 이 명부에 없음(이미 정리됨)", player->GetId());
		return;
	}

	RosterEntry& entry = it->second;
	if (entry.entered) return;

	if (entry.channel < 1 || entry.channel > static_cast<int>(_channels.size()))
	{
		LOG_ERROR("World", "EnterWorld: 세션 {} 의 채널 {} 이 없습니다", player->GetId(), entry.channel);
		return;
	}

	// 처음 들어가는 곳은 마을 Room이다. 더미 봇만 [Debug] DummyRoom으로 사냥터에 바로 넣을 수 있다(부하 시험).
	const Channel& channel = _channels[entry.channel - 1];
	shared_ptr<Room> room = channel.town;
	if (entry.isDummy && channel.field != nullptr)
	{
		const string& mode = GServerConfig.dummyRoom;
		if (mode == "field" || (mode == "split" && (_dummySeq++ % 2) == 1))
			room = channel.field;
	}
	entry.entered = true;
	entry.room = room;

	// 방 배정을 먼저 적어 두어야 이 뒤에 끊긴 세션이 퇴장 잡을 그 방에 넣는다(GameSession::HandlePlayerGone).
	// 그 전에 끊긴 세션은 입장 잡이 끊김을 보고 취소한다(Room::PlayerEnterRoom).
	player->SetOwnerRoom(room);
	room->PushJob(&Room::PlayerEnterRoom, player);
}

void WorldService::OnPlayerClosed(int sessionId, int64_t playerUID, bool isDummy)
{
	const auto it = _roster.find(sessionId);
	if (it != _roster.end())
	{
		const RosterEntry& entry = it->second;
		if (entry.channel >= 1 && entry.channel <= static_cast<int>(_channels.size()))
			--_channels[entry.channel - 1].population;

		// 색인은 이 세션의 것일 때만 지운다(강제 제거 뒤 같은 캐릭터의 새 접속이 이미 덮어썼을 수 있다).
		if (const auto uidIt = _uidIndex.find(entry.playerUID); uidIt != _uidIndex.end() && uidIt->second == sessionId)
			_uidIndex.erase(uidIt);
		if (const auto nameIt = _nameIndex.find(entry.name); nameIt != _nameIndex.end() && nameIt->second == sessionId)
			_nameIndex.erase(nameIt);

		LOG_DEBUG("World", "세션 {} 정리: 채널 {}", sessionId, entry.channel);
		_roster.erase(it);
		PublishMetrics();
	}

	// 중복 접속 목록(_online)에서 빼는 것은 로비가 한다. 명부에 없던 신호도 그대로 넘긴다(로비가 세션 id로 다시 확인한다).
	if (false == isDummy && playerUID != 0)
		GLobby->PushJob(&AuthLobby::OnPlayerClosed, playerUID, sessionId);
}

void WorldService::RequestMove(shared_ptr<Player> player, shared_ptr<Room> source, int channel, string destArea)
{
	if (player == nullptr || source == nullptr) return;

	shared_ptr<Room> dest;
	if (channel >= 1 && channel <= static_cast<int>(_channels.size()))
	{
		const Channel& ch = _channels[channel - 1];
		if (destArea == AREA_TOWN) dest = ch.town;
		else if (destArea == AREA_FIELD) dest = ch.field;
	}

	// 결과(없으면 nullptr)를 출발 Room에 알린다. 떠나기·취소 판단은 그 Room이 한다.
	if (GServerConfig.moveHandoffDelayMs > 0)
		source->ReserveJob(static_cast<DWORD>(GServerConfig.moveHandoffDelayMs), &Room::OnMoveResolved, player, dest);
	else
		source->PushJob(&Room::OnMoveResolved, player, dest);
}

void WorldService::OnPlayerMoved(int sessionId, shared_ptr<Room> room)
{
	const auto it = _roster.find(sessionId);
	if (it == _roster.end()) return;   // 그 사이 접속이 끝나 정리됐다
	it->second.room = room;
}

void WorldService::PublishMetrics()
{
	int maxPopulation = 0;
	for (const Channel& channel : _channels)
		maxPopulation = max(maxPopulation, channel.population);

	Metrics::WorldChannels.store(static_cast<int>(_channels.size()));
	Metrics::WorldRoster.store(static_cast<int>(_roster.size()));
	Metrics::WorldChannelMax.store(maxPopulation);
}
