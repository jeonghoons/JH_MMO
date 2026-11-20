#include "pch.h"
#include "Game/World/WorldService.h"
#include "Game/Room/Room.h"
#include "Game/Object/Player.h"
#include "Game/Lobby/AuthLobby.h"
#include "Utils/Metrics.h"
#include "Packet/PacketSerializer.h"
#include "Session/GameSession.h"

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
	entry.level = player->GetInfo().level();

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
		RosterEntry& entry = it->second;

		// 파티(DG2): 접속이 끝나면 탈퇴한다. 이 세션이 낀 대기 초대도 지운다.
		DropInvitesOf(sessionId);
		if (entry.partyId != 0)
			RemoveFromParty(sessionId, entry, true);

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

	// 파티원이면 "있는 곳"이 바뀌었으므로 전원에게 다시 알린다(DG2).
	if (it->second.partyId != 0)
	{
		if (const auto partyIt = _parties.find(it->second.partyId); partyIt != _parties.end())
			BroadcastPartyState(partyIt->second);
	}
}

void WorldService::PublishMetrics()
{
	int maxPopulation = 0;
	for (const Channel& channel : _channels)
		maxPopulation = max(maxPopulation, channel.population);

	Metrics::WorldChannels.store(static_cast<int>(_channels.size()));
	Metrics::WorldRoster.store(static_cast<int>(_roster.size()));
	Metrics::WorldChannelMax.store(maxPopulation);

	int partyMembers = 0;
	for (const auto& [id, party] : _parties)
		partyMembers += static_cast<int>(party.members.size());
	Metrics::Parties.store(static_cast<int>(_parties.size()));
	Metrics::PartyMembers.store(partyMembers);
	Metrics::PartyInvitesPending.store(static_cast<int>(_invites.size()));
}

// ---------------------------------------------------------------------------------------------------------
// 파티 (DG2, D-217 — DESIGN_PARTY_DUNGEON §5). 상태는 모두 이 서비스의 잡 안에서만 읽고 쓴다.
// 패킷은 세션에 직접 보낸다(이 서비스의 잡 큐가 묶어 내보낸다).
// 결과 패킷의 request: 초대한 쪽에게 가는 것은 CS_PARTY_INVITE, 초대받은 쪽에게 가는 것은 CS_PARTY_RESPOND(클라이언트가 초대 창을 닫는다).
// ---------------------------------------------------------------------------------------------------------
namespace
{
	const long long PARTY_INVITE_MIN_INTERVAL_MS = 1000;

	long long NowMs()
	{
		return chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	}

	Protocol::RoomKind ToProtoKind(const shared_ptr<Room>& room)
	{
		if (room == nullptr) return Protocol::ROOM_KIND_NONE;
		switch (room->GetInfo().kind)   // RoomInfo는 만들 때 정하고 바뀌지 않는다(어느 스레드에서 읽어도 된다)
		{
		case RoomKind::Town: return Protocol::ROOM_KIND_TOWN;
		case RoomKind::Field: return Protocol::ROOM_KIND_FIELD;
		case RoomKind::Dungeon: return Protocol::ROOM_KIND_DUNGEON;
		}
		return Protocol::ROOM_KIND_NONE;
	}
}

void WorldService::SendTo(const RosterEntry& entry, shared_ptr<SendBuffer> buffer)
{
	if (const shared_ptr<GameSession> session = entry.session.lock())
		session->Send(buffer);
}

void WorldService::SendPartyResult(int sessionId, Protocol::PacketId request, Protocol::PartyResult result)
{
	const auto it = _roster.find(sessionId);
	if (it == _roster.end()) return;
	LOG_DEBUG("Party", "결과 session={} request={} result={}", sessionId, Protocol::PacketId_Name(request), Protocol::PartyResult_Name(result));
	SendTo(it->second, PacketSerializer::MAKE_SC_PARTY_RESULT(request, result));
}

void WorldService::BroadcastPartyState(const Party& party)
{
	Protocol::SC_PARTY_STATE_PACKET packet;
	packet.set_party_id(party.id);
	packet.set_leader_id(party.leader);
	for (const int memberId : party.members)
	{
		const auto it = _roster.find(memberId);
		if (it == _roster.end()) continue;   // 구성원은 접속이 끝날 때 먼저 빠지므로 오지 않는다
		Protocol::PartyMember* member = packet.add_members();
		member->set_id(memberId);
		member->set_name(Utils::WStringToUTF8(it->second.name));
		member->set_level(it->second.level);
		member->set_kind(ToProtoKind(it->second.room.lock()));
	}

	const shared_ptr<SendBuffer> buffer = PacketSerializer::MAKE_SC_PARTY_STATE(packet);
	for (const int memberId : party.members)
	{
		if (const auto it = _roster.find(memberId); it != _roster.end())
			SendTo(it->second, buffer);
	}
}

void WorldService::SetPartyId(int sessionId, RosterEntry& entry, int partyId)
{
	entry.partyId = partyId;

	// Player의 필드는 소유 Room 잡에서만 만진다. 이동 중이면 받은 Room이 소유 Room으로 넘긴다(Room::SetPlayerParty).
	// 알림이 Room을 건너가며 순서가 바뀔 수 있어 순번을 붙인다(큰 순번만 적용).
	const shared_ptr<GameSession> session = entry.session.lock();
	if (session == nullptr) return;
	const shared_ptr<Player> player = session->GetPlayer();
	if (player == nullptr) return;
	if (const shared_ptr<Room> room = player->GetCurrentRoom())
		room->PushJob(&Room::SetPlayerParty, player, partyId, ++_partySeq);
}

void WorldService::AddToParty(Party& party, int sessionId, RosterEntry& entry)
{
	party.members.push_back(sessionId);
	SetPartyId(sessionId, entry, party.id);

	// 파티에 든 사람에게 와 있던 다른 초대는 더 받을 수 없다.
	if (const auto inviteIt = _invites.find(sessionId); inviteIt != _invites.end())
	{
		const int from = inviteIt->second.from;
		_invites.erase(inviteIt);
		SendPartyResult(from, Protocol::CS_PARTY_INVITE, Protocol::PARTY_RESULT_TARGET_IN_PARTY);
		SendPartyResult(sessionId, Protocol::CS_PARTY_RESPOND, Protocol::PARTY_RESULT_NO_INVITE);
	}
}

void WorldService::RemoveFromParty(int sessionId, RosterEntry& entry, bool closed)
{
	const int partyId = entry.partyId;
	const auto partyIt = _parties.find(partyId);

	if (closed)
		entry.partyId = 0;   // 접속이 끝났다 — Room에 알릴 것도 본인에게 보낼 것도 없다
	else
	{
		SetPartyId(sessionId, entry, 0);
		SendTo(entry, PacketSerializer::MAKE_SC_PARTY_STATE(Protocol::SC_PARTY_STATE_PACKET()));   // 파티원 0명 = 파티 없음
	}
	if (partyIt == _parties.end()) return;

	Party& party = partyIt->second;
	party.members.erase(remove(party.members.begin(), party.members.end(), sessionId), party.members.end());
	LOG_DEBUG("Party", "파티 {} 에서 session {} 나감({}), 남은 {}명", partyId, sessionId, closed ? "접속 해제" : "탈퇴", party.members.size());

	// 한 명만 남으면 파티가 없어진다.
	if (party.members.size() <= 1)
	{
		for (const int lastId : party.members)
		{
			if (const auto it = _roster.find(lastId); it != _roster.end())
			{
				SetPartyId(lastId, it->second, 0);
				SendTo(it->second, PacketSerializer::MAKE_SC_PARTY_STATE(Protocol::SC_PARTY_STATE_PACKET()));
			}
		}
		_parties.erase(partyIt);
		Metrics::PartyDisbanded.fetch_add(1);
		return;
	}

	// 파티장이 나갔으면 가장 먼저 들어온 사람이 파티장.
	if (party.leader == sessionId)
		party.leader = party.members.front();
	BroadcastPartyState(party);
}

void WorldService::DropInvitesOf(int sessionId)
{
	// 받은 초대
	if (const auto it = _invites.find(sessionId); it != _invites.end())
	{
		const int from = it->second.from;
		_invites.erase(it);
		SendPartyResult(from, Protocol::CS_PARTY_INVITE, Protocol::PARTY_RESULT_NO_TARGET);
	}
	// 보낸 초대(여러 건일 수 있다)
	for (auto it = _invites.begin(); it != _invites.end();)
	{
		if (it->second.from != sessionId) { ++it; continue; }
		const int target = it->first;
		it = _invites.erase(it);
		SendPartyResult(target, Protocol::CS_PARTY_RESPOND, Protocol::PARTY_RESULT_NO_TARGET);
	}
}

void WorldService::PartyInvite(shared_ptr<Player> player, int targetId, wstring targetName)
{
	if (player == nullptr) return;
	const int myId = player->GetId();
	const auto myIt = _roster.find(myId);
	if (myIt == _roster.end() || false == myIt->second.entered) return;   // 정리됐거나 아직 방에 들어가기 전
	RosterEntry& me = myIt->second;

	auto reject = [&](Protocol::PartyResult result) {
		Metrics::PartyRejected.fetch_add(1);
		SendPartyResult(myId, Protocol::CS_PARTY_INVITE, result);
	};

	// 검증 순서: 간격 → 대상 → 채널 → 내 파티(파티장·정원) → 대상의 파티 → 대상의 대기 초대
	const long long nowMs = NowMs();
	if (nowMs - me.lastInviteMs < PARTY_INVITE_MIN_INTERVAL_MS) { reject(Protocol::PARTY_RESULT_TOO_FREQUENT); return; }
	me.lastInviteMs = nowMs;

	int targetSessionId = targetId;
	if (targetSessionId == 0)
	{
		const auto nameIt = _nameIndex.find(targetName);
		if (nameIt != _nameIndex.end()) targetSessionId = nameIt->second;
	}
	const auto targetIt = targetSessionId != 0 ? _roster.find(targetSessionId) : _roster.end();
	if (targetIt == _roster.end() || targetSessionId == myId || false == targetIt->second.entered) { reject(Protocol::PARTY_RESULT_NO_TARGET); return; }
	RosterEntry& target = targetIt->second;

	if (target.channel != me.channel) { reject(Protocol::PARTY_RESULT_OTHER_CHANNEL); return; }

	const EconomyData& economy = DataManager::GetEconomy();
	if (me.partyId != 0)
	{
		const auto partyIt = _parties.find(me.partyId);
		if (partyIt != _parties.end())
		{
			if (partyIt->second.leader != myId) { reject(Protocol::PARTY_RESULT_NOT_LEADER); return; }
			if (static_cast<int>(partyIt->second.members.size()) >= economy.partyMaxMembers) { reject(Protocol::PARTY_RESULT_PARTY_FULL); return; }
		}
	}
	if (target.partyId != 0) { reject(Protocol::PARTY_RESULT_TARGET_IN_PARTY); return; }
	if (_invites.count(targetSessionId) > 0) { reject(Protocol::PARTY_RESULT_TARGET_BUSY); return; }

	PendingInvite invite;
	invite.from = myId;
	invite.seq = ++_inviteSeq;
	_invites[targetSessionId] = invite;
	Metrics::PartyInvites.fetch_add(1);
	PublishMetrics();

	LOG_DEBUG("Party", "초대 {} → {}", myId, targetSessionId);
	SendTo(target, PacketSerializer::MAKE_SC_PARTY_INVITE(myId, me.name, economy.partyInviteTimeoutSec));
	ReserveJob(static_cast<DWORD>(economy.partyInviteTimeoutSec) * 1000, &WorldService::OnInviteTimeout, targetSessionId, invite.seq);
}

void WorldService::PartyRespond(shared_ptr<Player> player, bool accept)
{
	if (player == nullptr) return;
	const int myId = player->GetId();
	const auto myIt = _roster.find(myId);
	if (myIt == _roster.end()) return;
	RosterEntry& me = myIt->second;

	const auto inviteIt = _invites.find(myId);
	if (inviteIt == _invites.end()) { SendPartyResult(myId, Protocol::CS_PARTY_RESPOND, Protocol::PARTY_RESULT_NO_INVITE); return; }
	const int fromId = inviteIt->second.from;
	_invites.erase(inviteIt);

	const auto fromIt = _roster.find(fromId);
	if (fromIt == _roster.end())
	{
		// 보낸 사람이 끊기면 초대도 함께 지우므로(DropInvitesOf) 오지 않는다.
		SendPartyResult(myId, Protocol::CS_PARTY_RESPOND, Protocol::PARTY_RESULT_NO_TARGET);
		PublishMetrics();
		return;
	}
	RosterEntry& from = fromIt->second;

	if (false == accept)
	{
		Metrics::PartyDeclined.fetch_add(1);
		SendPartyResult(fromId, Protocol::CS_PARTY_INVITE, Protocol::PARTY_RESULT_DECLINED);
		PublishMetrics();
		return;
	}

	// 초대를 받은 뒤 사정이 바뀌었을 수 있어 다시 본다. (파티에 든 사람의 대기 초대는 AddToParty가 지우므로 me.partyId는 0이다)
	auto fail = [&](Protocol::PartyResult result) {
		Metrics::PartyRejected.fetch_add(1);
		SendPartyResult(myId, Protocol::CS_PARTY_RESPOND, result);
		PublishMetrics();
	};
	if (me.partyId != 0) { fail(Protocol::PARTY_RESULT_TARGET_IN_PARTY); return; }

	Party* party = nullptr;
	if (from.partyId != 0)
	{
		const auto partyIt = _parties.find(from.partyId);
		if (partyIt == _parties.end()) { fail(Protocol::PARTY_RESULT_NO_INVITE); return; }
		party = &partyIt->second;
		if (party->leader != fromId) { fail(Protocol::PARTY_RESULT_NOT_LEADER); return; }   // 초대한 뒤 다른 파티의 파티원이 됐다
		if (static_cast<int>(party->members.size()) >= DataManager::GetEconomy().partyMaxMembers) { fail(Protocol::PARTY_RESULT_PARTY_FULL); return; }
	}
	else
	{
		// 파티가 없던 사람의 초대가 수락됐다 → 그 사람이 파티장인 파티가 생긴다.
		Party created;
		created.id = ++_nextPartyId;
		created.leader = fromId;
		party = &_parties.emplace(created.id, std::move(created)).first->second;
		AddToParty(*party, fromId, from);
		Metrics::PartyCreated.fetch_add(1);
	}

	AddToParty(*party, myId, me);
	Metrics::PartyAccepted.fetch_add(1);
	LOG_DEBUG("Party", "파티 {}: {} 의 초대를 {} 가 수락, {}명", party->id, fromId, myId, party->members.size());
	BroadcastPartyState(*party);
	PublishMetrics();
}

void WorldService::PartyLeave(shared_ptr<Player> player)
{
	if (player == nullptr) return;
	const int myId = player->GetId();
	const auto myIt = _roster.find(myId);
	if (myIt == _roster.end()) return;

	if (myIt->second.partyId == 0) { SendPartyResult(myId, Protocol::CS_PARTY_LEAVE, Protocol::PARTY_RESULT_NOT_IN_PARTY); return; }
	RemoveFromParty(myId, myIt->second, false);
	PublishMetrics();
}

void WorldService::OnInviteTimeout(int targetSessionId, int seq)
{
	const auto it = _invites.find(targetSessionId);
	if (it == _invites.end() || it->second.seq != seq) return;

	const int fromId = it->second.from;
	_invites.erase(it);
	Metrics::PartyTimeout.fetch_add(1);
	SendPartyResult(fromId, Protocol::CS_PARTY_INVITE, Protocol::PARTY_RESULT_TIMEOUT);
	SendPartyResult(targetSessionId, Protocol::CS_PARTY_RESPOND, Protocol::PARTY_RESULT_TIMEOUT);
	PublishMetrics();
}
