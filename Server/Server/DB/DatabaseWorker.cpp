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
	int32_t outEquipSlot = 0;
	int32_t outItemId = 0;
	SQLLEN len[12] = { 0 };

	dbConn->BindCol(1, outPassword, sizeof(outPassword), &len[0]);
	dbConn->BindCol(2, &outPlayerInfo.playerUID, &len[1]);
	dbConn->BindCol(3, &outPlayerInfo.accountUID, &len[2]);
	dbConn->BindCol(4, outPlayerInfo.playerName, sizeof(outPlayerInfo.playerName), &len[3]);
	dbConn->BindCol(5, &outPlayerInfo.playerType, &len[4]);

	dbConn->BindCol(6, &outPlayerData.level, &len[5]);
	dbConn->BindCol(7, &outPlayerData.exp, &len[6]);
	dbConn->BindCol(8, &outPlayerData.hp, &len[7]);
	dbConn->BindCol(9, &outPlayerData.gold, &len[8]);
	
	dbConn->BindCol(10, &outEquipSlot, &len[9]);
	dbConn->BindCol(11, &outItemId, &len[10]);
	dbConn->BindCol(12, &outPlayerData.neverSaved, &len[11]);

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

			if (loginSuccess && outItemId != 0)
			{
				outPlayerData.equipped.emplace_back(outEquipSlot, outItemId);
			}
		}

		if (loginSuccess)
		{
			GLobby->PushJob(&AuthLobby::OnLoginSuccess, session, outPlayerInfo, outPlayerData);
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


void DatabaseWorker::SaveEquipSnapshot(shared_ptr<Room> room, shared_ptr<Player> player, int64_t playerUID, vector<int32_t> items, long long requestUs)
{
	DBConnection* dbConn = _dbConnectionPool->Pop();
	dbConn->Unbind();

	SQLLEN uidLen = 0;
	dbConn->BindParam(1, &playerUID, &uidLen);

	const wstring query = DbQueries::BuildSaveEquipSnapshot(items);

	const long long startUs = Metrics::NowUs();
	const bool success = dbConn->Execute(query.c_str());
	const long long endUs = Metrics::NowUs();
	_dbConnectionPool->Push(dbConn);

	Metrics::EquipSaveExecUs.Add(endUs - startUs);        // DB 실행 시간
	Metrics::EquipSaveTotalUs.Add(endUs - requestUs);     // 요청(장착 성공·퇴장) → 저장 완료 (큐 대기 포함)
	if (!success)
	{
		Metrics::EquipSaveFailed.fetch_add(1);
		LOG_ERROR("DB", "장비 저장 실패 (PlayerId {})", playerUID);
	}

	room->PushJob(&Room::OnEquipSaved, player, success);
}

void DatabaseWorker::SavePlayerProgress(shared_ptr<Room> room, shared_ptr<Player> player, int64_t playerUID, DB_PlayerProgress progress, long long requestUs)
{
	DBConnection* dbConn = _dbConnectionPool->Pop();
	dbConn->Unbind();

	SQLLEN len[5] = { 0 };
	dbConn->BindParam(1, &progress.level, &len[0]);
	dbConn->BindParam(2, &progress.exp, &len[1]);
	dbConn->BindParam(3, &progress.hp, &len[2]);
	dbConn->BindParam(4, &progress.gold, &len[3]);
	dbConn->BindParam(5, &playerUID, &len[4]);

	const long long startUs = Metrics::NowUs();
	const bool success = dbConn->Execute(DbQueries::UpdatePlayerProgress);
	const long long endUs = Metrics::NowUs();
	_dbConnectionPool->Push(dbConn);

	Metrics::ProgressSaveExecUs.Add(endUs - startUs);        // DB 실행 시간
	Metrics::ProgressSaveTotalUs.Add(endUs - requestUs);     // 요청(주기·퇴장) → 저장 완료 (큐 대기 포함)
	if (!success)
	{
		Metrics::ProgressSaveFailed.fetch_add(1);
		LOG_ERROR("DB", "진행 상황 저장 실패 (PlayerId {}, Level {}, Exp {}, Hp {}, Gold {})", playerUID, progress.level, progress.exp, progress.hp, progress.gold);
	}

	room->PushJob(&Room::OnProgressSaved, player, progress, success);
}
