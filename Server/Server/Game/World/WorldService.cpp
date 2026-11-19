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
	if (CreateChannel(true) == nullptr)
		return false;
	PublishMetrics();
	return true;
}

WorldService::Channel* WorldService::CreateChannel(bool allowBroken)
{
	const int channelId = static_cast<int>(_channels.size()) + 1;

	RoomInfo info;
	info.roomId = ++_nextRoomId;
	info.kind = RoomKind::Town;      // DG3a: 채널당 Room 하나(레벨 전체). DG3b에서 마을/사냥터로 나눈다
	info.channel = channelId;
	info.levelName = GServerConfig.mapName;   // 로그인한 플레이어가 처음 들어가는 레벨

	shared_ptr<Room> room = make_shared<Room>(GTimer, _iocpHandle, info);
	if (false == room->InitRoom(allowBroken))
		return nullptr;

	GRoomManager->Add(room);

	Channel channel;
	channel.id = channelId;
	channel.room = room;
	_channels.push_back(std::move(channel));
	LOG_INFO("World", "채널 {} 생성 (Room {}, 레벨 {})", channelId, info.roomId, info.levelName);
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

	const shared_ptr<Room>& room = _channels[entry.channel - 1].room;
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

void WorldService::PublishMetrics()
{
	int maxPopulation = 0;
	for (const Channel& channel : _channels)
		maxPopulation = max(maxPopulation, channel.population);

	Metrics::WorldChannels.store(static_cast<int>(_channels.size()));
	Metrics::WorldRoster.store(static_cast<int>(_roster.size()));
	Metrics::WorldChannelMax.store(maxPopulation);
}
