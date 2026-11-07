#include "pch.h"
#include "DB/DatabaseWorker.h"
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
	SQLLEN len[11] = { 0 };

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

	const WCHAR* query = L" \
        SELECT A.LoginPassword, P.PlayerId, P.AccountId, P.Name, P.PlayerType, \
               P.[Level], P.Exp, P.Hp, P.Gold, \
               ISNULL(E.EquipSlot, 0) AS EquipSlot, ISNULL(E.ItemId, 0) AS ItemId \
        FROM [User_Account] A \
        INNER JOIN [Player_Info] P ON A.AccountId = P.AccountId \
        LEFT JOIN [Player_Equipped] E ON P.PlayerId = E.PlayerId \
        WHERE A.LoginId = ?";

	if (dbConn->Execute(query))
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

	// 기본 장비(ItemTable.Starter)를 같은 트랜잭션에서 지급한다. 값은 로더가 검증한 정수라 쿼리에 그대로 넣는다. ItemDbId는 A3 전까지 0.
	wstring starterInsert;
	for (const ItemData* item : DataManager::GetStarterItems())
	{
		starterInsert += std::format(L"INSERT INTO [Player_Equipped] (PlayerId, EquipSlot, ItemDbId, ItemId) VALUES (@PlayerID, {}, 0, {}); ",
			static_cast<int>(item->slot), item->itemId);
	}

	const wstring query = wstring(L" \
        SET NOCOUNT ON; SET XACT_ABORT ON; \
        BEGIN TRAN; \
        DECLARE @AccID BIGINT, @PlayerID BIGINT; \
        INSERT INTO [User_Account] (LoginId, LoginPassword) VALUES (?, ?); \
        SET @AccID = SCOPE_IDENTITY(); \
        INSERT INTO [User_Info] (AccountId) VALUES (@AccID); \
        INSERT INTO [Player_Info] (AccountId, Name, PlayerType, [Level], Exp, Hp, Gold) \
        VALUES (@AccID, ?, ?, ?, 0, ?, 0); \
        SET @PlayerID = SCOPE_IDENTITY(); ") + starterInsert + L"COMMIT TRAN;";

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

	// 슬롯 = 인덱스 + 1 (Protocol::EquipPart). 값은 Room이 검증한 itemId라 쿼리에 그대로 넣는다. ItemDbId는 A3 전까지 0.
	wstring inserts;
	for (size_t i = 0; i < items.size(); ++i)
	{
		if (items[i] == 0) continue;
		inserts += std::format(L"INSERT INTO [Player_Equipped] (PlayerId, EquipSlot, ItemDbId, ItemId) VALUES (@P, {}, 0, {}); ", i + 1, items[i]);
	}
	const wstring query = L"SET NOCOUNT ON; SET XACT_ABORT ON; DECLARE @P BIGINT = ?; BEGIN TRAN; "
		L"DELETE FROM [Player_Equipped] WHERE PlayerId = @P; " + inserts + L"COMMIT TRAN;";

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
