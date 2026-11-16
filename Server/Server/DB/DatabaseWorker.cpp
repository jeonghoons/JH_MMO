#include "pch.h"
#include "DB/DatabaseWorker.h"
#include "DB/DbQueries.h"
#include "Game/Object/Player.h"
#include "Game/Room/Room.h"
#include "Utils/Metrics.h"

// 연결 문자열은 ServerConfig([Database] ConnectionString 또는 환경변수 JH_DB_CONNECTION)에서 받는다.
DatabaseWorker::DatabaseWorker(HANDLE iocpHandle, int num_connections, const wstring& connectionString) : _iocpHandle(iocpHandle)
{
	_dbConnectionPool = make_unique<DBConnectionPool>();
	if (false == _dbConnectionPool->Connect(num_connections, connectionString.c_str()))
	{
		LOG_ERROR("DB", "DB Connect 오류 !");
		exit(-1);
	}
	LOG_INFO("DB", "DB 서버 Connected");

	// 스키마가 코드와 맞지 않으면 시작하지 않는다(스레드를 만들기 전에 확인 — 시작 실패 시 exit()가 멈추지 않게).
	if (false == CheckSchemaVersion())
		exit(-1);

	for (int i = 0; i < num_connections; ++i) {
		_threads.emplace_back(&DatabaseWorker::Run, this);
	}
}

DatabaseWorker::~DatabaseWorker()
{
	for (auto& t : _threads) {
		if (t.joinable()) t.join();
	}
}

bool DatabaseWorker::CheckSchemaVersion()
{
	DBConnection* dbConn = _dbConnectionPool->Pop();
	dbConn->Unbind();

	int32_t dbVersion = -1;
	SQLLEN versionLen = 0;
	dbConn->BindCol(1, &dbVersion, &versionLen);
	const bool read = dbConn->Execute(DbQueries::SelectSchemaVersion) && dbConn->Fetch();
	dbConn->Unbind();
	_dbConnectionPool->Push(dbConn);

	if (!read)
	{
		LOG_ERROR("DB", "Schema_Version 테이블을 읽을 수 없습니다. Server/Resource/DB/migrate.ps1 을 실행해 DB를 버전 {}(으)로 올리세요.", DbQueries::DB_SCHEMA_VERSION);
		return false;
	}
	if (dbVersion != DbQueries::DB_SCHEMA_VERSION)
	{
		LOG_ERROR("DB", "DB 스키마 버전이 서버와 다릅니다 (DB {}, 서버 {}). DB가 낮으면 Server/Resource/DB/migrate.ps1 을 실행하고, 높으면 서버 코드를 최신으로 받으세요.",
			dbVersion, DbQueries::DB_SCHEMA_VERSION);
		return false;
	}
	LOG_INFO("DB", "DB 스키마 버전 {}", dbVersion);
	return true;
}

void DatabaseWorker::Run()
{
	while (true)
	{
		shared_ptr<Job> job = nullptr;

		_dbJobQueue.WaitPop(job);

		if (job) {
			job->Execute();
		}
	}
}


void DatabaseWorker::TryLogin(shared_ptr<GameSession> session, string recvId, string recvPw)
{
	DBConnection* dbConn = _dbConnectionPool->Pop();
	dbConn->Unbind();

	WCHAR loginId[50] = {};
	MultiByteToWideChar(CP_UTF8, 0, recvId.c_str(), -1, loginId, _countof(loginId));
	SQLLEN loginIdLen = 0;
	dbConn->BindParam(1, loginId, &loginIdLen);

	WCHAR outPassword[50] = {};
	DB_PlayerInfo outPlayerInfo = {};
	DB_PlayerData outPlayerData = {};
	DB_ItemRow outItem;
	SQLLEN len[16 + QUICK_SLOT_COUNT] = { 0 };

	dbConn->BindCol(1, outPassword, sizeof(outPassword), &len[0]);
	dbConn->BindCol(2, &outPlayerInfo.playerUID, &len[1]);
	dbConn->BindCol(3, &outPlayerInfo.accountUID, &len[2]);
	dbConn->BindCol(4, outPlayerInfo.playerName, sizeof(outPlayerInfo.playerName), &len[3]);
	dbConn->BindCol(5, &outPlayerInfo.playerType, &len[4]);

	dbConn->BindCol(6, &outPlayerData.level, &len[5]);
	dbConn->BindCol(7, &outPlayerData.exp, &len[6]);
	dbConn->BindCol(8, &outPlayerData.hp, &len[7]);
	dbConn->BindCol(9, &outPlayerData.gold, &len[8]);
	
	dbConn->BindCol(10, &outItem.itemDbId, &len[9]);
	dbConn->BindCol(11, &outItem.itemId, &len[10]);
	dbConn->BindCol(12, &outItem.count, &len[11]);
	dbConn->BindCol(13, &outItem.location, &len[12]);
	dbConn->BindCol(14, &outItem.equipSlot, &len[13]);
	dbConn->BindCol(15, &outItem.bagSlot, &len[14]);
	dbConn->BindCol(16, &outPlayerData.neverSaved, &len[15]);
	for (int i = 0; i < QUICK_SLOT_COUNT; ++i)
		dbConn->BindCol(17 + i, &outPlayerData.quickSlots[i], &len[16 + i]);

	if (dbConn->Execute(DbQueries::SelectLogin))
	{
		bool isFirstRow = true;
		bool loginSuccess = false;

		while (dbConn->Fetch())
		{
			if (isFirstRow)
			{
				WCHAR clpassword[50] = {};
				MultiByteToWideChar(CP_UTF8, 0, recvPw.c_str(), -1, clpassword, _countof(clpassword));
				
				if (lstrcmpW(clpassword, outPassword) == 0)
				{
					outPlayerData.playerUID = outPlayerInfo.playerUID;
					loginSuccess = true;
				}
				else
				{
					GLobby->PushJob(&AuthLobby::OnLoginFailed, session, Protocol::LOGIN_RESULT_WRONG_PASSWORD);
					isFirstRow = false;   // 아래의 "없는 ID" 응답이 한 번 더 나가지 않게 한다.
					break; 
				}
				isFirstRow = false;
			}

			// 아이템이 없는 캐릭터는 LEFT JOIN의 빈 행(ItemDbId 0) 하나가 온다.
			if (loginSuccess && outItem.itemDbId != 0)
			{
				outPlayerData.items.push_back(outItem);
			}
		}

		if (loginSuccess)
		{
			GLobby->PushJob(&AuthLobby::OnLoginSuccess, session, outPlayerInfo, outPlayerData, recvId, recvPw);
		}
		else if (isFirstRow) 
		{
			GLobby->PushJob(&AuthLobby::OnLoginFailed, session, Protocol::LOGIN_RESULT_NO_ACCOUNT);
		}
	}
	else
	{
		GLobby->PushJob(&AuthLobby::OnLoginFailed, session, Protocol::LOGIN_RESULT_SERVER_ERROR);
	}
	_dbConnectionPool->Push(dbConn);
}

void DatabaseWorker::TrySignUP(shared_ptr<GameSession> session, string recvId, string recvPw, int playerType)
{
	DBConnection* dbConn = _dbConnectionPool->Pop();
	dbConn->Unbind();

	WCHAR loginId[50] = {};
	MultiByteToWideChar(CP_UTF8, 0, recvId.c_str(), -1, loginId, _countof(loginId));
	SQLLEN idLen = 0;
	dbConn->BindParam(1, loginId, &idLen);

	WCHAR password[256] = {};
	MultiByteToWideChar(CP_UTF8, 0, recvPw.c_str(), -1, password, _countof(password));
	SQLLEN pwLen = 0;
	dbConn->BindParam(2, password, &pwLen);

	dbConn->BindParam(3, loginId, &idLen); // 닉네임을 ID와 동일하게 설정

	int32_t cType = (int32_t)playerType;
	SQLLEN typeLen = 0;
	dbConn->BindParam(4, &cType, &typeLen);

	// 시작 레벨과 그 레벨의 최대 HP(PlayerStatTable, 로더가 시작 레벨 행을 보장)를 저장한다. SQL에 수치를 쓰지 않는다.
	int32_t startLevel = PLAYER_START_LEVEL;
	SQLLEN levelLen = 0;
	dbConn->BindParam(5, &startLevel, &levelLen);

	const StatData* startStat = DataManager::GetPlayerStatData(PLAYER_START_LEVEL);
	int32_t startHp = startStat ? startStat->maxHp : 1;
	SQLLEN hpLen = 0;
	dbConn->BindParam(6, &startHp, &hpLen);

	// 기본 장비(ItemTable.Starter)를 같은 트랜잭션에서 지급한다.
	vector<pair<int, int>> starterItems;
	for (const ItemData* item : DataManager::GetStarterItems())
		starterItems.emplace_back(static_cast<int>(item->slot), item->itemId);
	const wstring query = DbQueries::BuildSignUp(starterItems);

	const bool success = dbConn->Execute(query.c_str());
	if (success)
	{
		LOG_DEBUG("DB", "회원가입 및 캐릭터 자동생성 성공 - ID : {}", recvId);
	}
	else
	{
		LOG_DEBUG("DB", "중복 아이디 또는 가입 오류");
	}
	GLobby->PushJob(&AuthLobby::OnSignUpResult, session, success);

	_dbConnectionPool->Push(dbConn);
}


void DatabaseWorker::ExecItemOp(shared_ptr<Room> room, shared_ptr<Player> player, DB_ItemOp op)
{
	DBConnection* dbConn = _dbConnectionPool->Pop();
	dbConn->Unbind();

	// 공통 파라미터: 1 PlayerId, 2 Fail(시험용 강제 롤백)
	int64_t playerUID = op.playerUID;
	int32_t forceFail = op.forceFail ? 1 : 0;
	SQLLEN paramLen[10] = { 0 };
	// 줍기 고정 쿼리의 파라미터(없는 것은 0). Execute가 끝날 때까지 살아 있어야 한다.
	int64_t fillDbId = 0;
	int32_t fillExpect = 0, fillAdd = 0, newCount = 0, newSlot = -1;
	dbConn->BindParam(1, &playerUID, &paramLen[0]);
	dbConn->BindParam(2, &forceFail, &paramLen[1]);

	wstring built;
	const wchar_t* query = nullptr;
	const char* kindName = "";
	switch (op.kind)
	{
	case ItemOpKind::Equip:
		kindName = "Equip";
		dbConn->BindParam(3, &op.equipPart, &paramLen[2]);
		dbConn->BindParam(4, &op.newDbId, &paramLen[3]);
		dbConn->BindParam(5, &op.oldDbId, &paramLen[4]);
		dbConn->BindParam(6, &op.offhandDbId, &paramLen[5]);
		dbConn->BindParam(7, &op.newSlot, &paramLen[6]);
		dbConn->BindParam(8, &op.oldSlot, &paramLen[7]);
		dbConn->BindParam(9, &op.offhandSlot, &paramLen[8]);
		query = DbQueries::ItemEquip.c_str();
		break;
	case ItemOpKind::Move:
		kindName = "Move";
		dbConn->BindParam(3, &op.moveDbId, &paramLen[2]);
		dbConn->BindParam(4, &op.moveFrom, &paramLen[3]);
		dbConn->BindParam(5, &op.moveTo, &paramLen[4]);
		dbConn->BindParam(6, &op.moveOtherDbId, &paramLen[5]);
		query = DbQueries::ItemMove.c_str();
		break;
	case ItemOpKind::Pickup:
		kindName = "Pickup";
		if (op.fills.size() > 1 || op.newCounts.size() > 1)
		{
			// 스택 둘 이상·새 행 둘 이상(드묾)은 지급과 같은 조립식 쿼리로 한다.
			built = DbQueries::BuildItemGive(op);
			query = built.c_str();
			break;
		}
		if (!op.fills.empty()) { fillDbId = op.fills[0].itemDbId; fillExpect = op.fills[0].expectCount; fillAdd = op.fills[0].add; }
		if (!op.newCounts.empty()) { newCount = op.newCounts[0]; newSlot = op.newSlots[0]; }
		dbConn->BindParam(3, &op.itemId, &paramLen[2]);
		dbConn->BindParam(4, &fillDbId, &paramLen[3]);
		dbConn->BindParam(5, &fillExpect, &paramLen[4]);
		dbConn->BindParam(6, &fillAdd, &paramLen[5]);
		dbConn->BindParam(7, &newCount, &paramLen[6]);
		dbConn->BindParam(8, &newSlot, &paramLen[7]);
		dbConn->BindParam(9, &op.goldDelta, &paramLen[8]);
		dbConn->BindParam(10, &op.goldMax, &paramLen[9]);
		query = DbQueries::ItemPickup.c_str();
		break;
	case ItemOpKind::Discard:
		kindName = "Discard";
		dbConn->BindParam(3, &op.discardDbId, &paramLen[2]);
		dbConn->BindParam(4, &op.discardSlot, &paramLen[3]);
		dbConn->BindParam(5, &op.discardCount, &paramLen[4]);
		query = DbQueries::ItemDiscard.c_str();
		break;
	case ItemOpKind::Use:
		kindName = "Use";
		dbConn->BindParam(3, &op.useDbId, &paramLen[2]);
		dbConn->BindParam(4, &op.useExpectCount, &paramLen[3]);
		query = DbQueries::ItemUse.c_str();
		break;
	case ItemOpKind::Trade:
		kindName = "Trade";
		built = DbQueries::BuildItemTrade(op);
		query = built.c_str();
		break;
	case ItemOpKind::Give:
		kindName = "Give";
		built = DbQueries::BuildItemGive(op);
		query = built.c_str();
		break;
	case ItemOpKind::LoginFix:
		kindName = "LoginFix";
		built = DbQueries::BuildItemLoginFix(op);
		query = built.c_str();
		break;
	}

	// 결과 행: Ok, Kind(0 상태 · 1 새 ItemDbId · 2 골드 잔액), V  (DbQueries.h "아이템 트랜잭션")
	int32_t outOk = 0;
	int32_t outKind = 0;
	int64_t outValue = 0;
	SQLLEN colLen[3] = { 0 };
	dbConn->BindCol(1, &outOk, &colLen[0]);
	dbConn->BindCol(2, &outKind, &colLen[1]);
	dbConn->BindCol(3, &outValue, &colLen[2]);

	// 시험용: 실행을 일부러 늦춘다([Debug] DbSaveDelayMs — 결과 도착 전 접속 해제·빠른 재접속 시험)
	if (GServerConfig.dbSaveDelayMs > 0) ::Sleep(GServerConfig.dbSaveDelayMs);

	DB_ItemOpResult result;
	bool sawStatus = false;   // 성공 상태 행(Ok 1, Kind 0)을 읽었다
	bool sawFail = false;
	int64_t errorCode = 0;

	const long long startUs = Metrics::NowUs();
	const bool executed = query != nullptr && dbConn->Execute(query);
	if (executed)
	{
		// 끝까지 읽는다(중간에 멈추면 배치의 나머지 결과가 연결에 남는다).
		while (dbConn->Fetch())
		{
			if (outOk != 1) { sawFail = true; errorCode = outValue; continue; }
			if (outKind == 0) sawStatus = true;
			else if (outKind == 1) result.newDbIds.push_back(outValue);
			else if (outKind == 2) result.gold = outValue;
			else if (outKind == 3) result.gold2 = outValue;
		}
	}
	const long long endUs = Metrics::NowUs();
	_dbConnectionPool->Push(dbConn);

	// 성공 행을 읽었을 때만 성공이다. 실행 실패·행 없음·실패 행은 모두 "바뀐 것 없음"(롤백)으로 본다.
	result.success = executed && sawStatus && !sawFail;
	// 실패 행(Ok 0)을 읽었으면 롤백이 확인된 실패다. 실행 오류이거나 행을 하나도 못 읽었으면 COMMIT 여부를 알 수 없다.
	result.unknown = !result.success && !sawFail;
	if (!result.success)
	{
		result.newDbIds.clear();
		result.gold = -1;
		result.gold2 = -1;
		if (op.forceFail)
			LOG_DEBUG("DB", "아이템 작업 강제 롤백 (PlayerId {}, {})", playerUID, kindName);
		else
			LOG_ERROR("DB", "아이템 작업 실패 (PlayerId {}, {}, executed {}, 오류 번호 {} — 0이면 조건 불일치로 롤백)", playerUID, kindName, executed, errorCode);
	}

	Metrics::ItemOpExecUs.Add(endUs - startUs);   // DB 실행 시간

	room->PushJob(&Room::OnItemOpDone, player, op, result);
}

void DatabaseWorker::SaveQuickSlots(shared_ptr<Room> room, shared_ptr<Player> player, int64_t playerUID, QuickSlotArray slots)
{
	DBConnection* dbConn = _dbConnectionPool->Pop();
	dbConn->Unbind();

	// 파라미터: 1~5 칸별 ItemId, 6·7 PlayerId (DbQueries::SaveQuickSlots)
	SQLLEN len[QUICK_SLOT_COUNT + 2] = { 0 };
	for (int i = 0; i < QUICK_SLOT_COUNT; ++i)
		dbConn->BindParam(1 + i, &slots[i], &len[i]);
	dbConn->BindParam(QUICK_SLOT_COUNT + 1, &playerUID, &len[QUICK_SLOT_COUNT]);
	dbConn->BindParam(QUICK_SLOT_COUNT + 2, &playerUID, &len[QUICK_SLOT_COUNT + 1]);

	if (GServerConfig.dbSaveDelayMs > 0) ::Sleep(GServerConfig.dbSaveDelayMs);

	const bool success = dbConn->Execute(DbQueries::SaveQuickSlots);
	_dbConnectionPool->Push(dbConn);
	if (!success)
		LOG_ERROR("DB", "액티브 칸 저장 실패 (PlayerId {}, 칸 {} {} {} {} {})", playerUID, slots[0], slots[1], slots[2], slots[3], slots[4]);

	room->PushJob(&Room::OnQuickSlotsSaved, player, slots, success);
}

void DatabaseWorker::SavePlayerProgress(shared_ptr<Room> room, shared_ptr<Player> player, int64_t playerUID, DB_PlayerProgress progress, long long requestUs)
{
	DBConnection* dbConn = _dbConnectionPool->Pop();
	dbConn->Unbind();

	SQLLEN len[4] = { 0 };
	dbConn->BindParam(1, &progress.level, &len[0]);
	dbConn->BindParam(2, &progress.exp, &len[1]);
	dbConn->BindParam(3, &progress.hp, &len[2]);
	dbConn->BindParam(4, &playerUID, &len[3]);

	if (GServerConfig.dbSaveDelayMs > 0) ::Sleep(GServerConfig.dbSaveDelayMs);

	const long long startUs = Metrics::NowUs();
	const bool success = dbConn->Execute(DbQueries::UpdatePlayerProgress);
	const long long endUs = Metrics::NowUs();
	_dbConnectionPool->Push(dbConn);

	Metrics::ProgressSaveExecUs.Add(endUs - startUs);        // DB 실행 시간
	Metrics::ProgressSaveTotalUs.Add(endUs - requestUs);     // 요청(주기·퇴장) → 저장 완료 (큐 대기 포함)
	if (!success)
	{
		Metrics::ProgressSaveFailed.fetch_add(1);
		LOG_ERROR("DB", "진행 상황 저장 실패 (PlayerId {}, Level {}, Exp {}, Hp {})", playerUID, progress.level, progress.exp, progress.hp);
	}

	room->PushJob(&Room::OnProgressSaved, player, progress, success);
}
