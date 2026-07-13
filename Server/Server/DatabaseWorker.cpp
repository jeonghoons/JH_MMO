#include "pch.h"
#include "DatabaseWorker.h"
#include "Player.h"

DatabaseWorker::DatabaseWorker(HANDLE iocpHandle, int num_connections) : _iocpHandle(iocpHandle)
{
	// const WCHAR* connectionPath = L"Driver={ODBC Driver 17 for SQL Server};Server=DESKTOP-0I9E8CG\\SQLEXPRESS;Database=GameServer;Trusted_Connection=Yes;";
	const WCHAR* connectionPath = L"Driver={ODBC Driver 17 for SQL Server};Server=.\\SQLEXPRESS;Database=GameServer;Trusted_Connection=Yes;";
	_dbConnectionPool = make_unique<DBConnectionPool>();
	if (false == _dbConnectionPool->Connect(num_connections, connectionPath))
	{
		std::cout << "DB Connect 오류 !" << std::endl;
		exit(-1);
	}
	std::cout << "DB 서버 Connected" << std::endl;

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


void DatabaseWorker::TryLogin(shared_ptr<Session> session, string recvId, string recvPw)
{
	DBConnection* dbConn = _dbConnectionPool->Pop();
	dbConn->Unbind();

	WCHAR loginId[20] = {};
	MultiByteToWideChar(CP_ACP, 0, recvId.c_str(), -1, loginId, _countof(loginId));
	SQLLEN loginIdLen = 0;
	dbConn->BindParam(1, loginId, &loginIdLen);

	// 1. 데이터를 받을 구조체 및 버퍼 준비
	WCHAR outPassword[20] = {};
	DB_PlayerInfo outPlayerInfo = {};
	DB_PlayerData outPlayerData = {};
	SQLLEN len[12] = { 0 };

	// 바인딩 (순서가 SELECT 문의 컬럼 순서와 정확히 일치해야 합니다)
	dbConn->BindCol(1, outPassword, sizeof(outPassword), &len[0]);
	dbConn->BindCol(2, &outPlayerInfo.playerUID, &len[1]);
	dbConn->BindCol(3, &outPlayerInfo.accountUID, &len[2]);
	dbConn->BindCol(4, outPlayerInfo.playerName, sizeof(outPlayerInfo.playerName), &len[3]);
	dbConn->BindCol(5, &outPlayerInfo.playerType, &len[4]);

	dbConn->BindCol(6, &outPlayerData.level, &len[5]);
	dbConn->BindCol(7, &outPlayerData.exp, &len[6]);
	dbConn->BindCol(8, &outPlayerData.hp, &len[7]);
	dbConn->BindCol(9, &outPlayerData.mp, &len[8]);
	dbConn->BindCol(10, &outPlayerData.posX, &len[9]);
	dbConn->BindCol(11, &outPlayerData.posY, &len[10]);
	dbConn->BindCol(12, &outPlayerData.posZ, &len[11]);

	const WCHAR* query = L" \
        SELECT A.LoginPassword, P.PlayerId, P.AccountId, P.Name, P.playerType, \
               P.[Level], P.Exp, P.Hp, P.Mp, P.PosX, P.PosY, P.PosZ \
        FROM [User_Account] A \
        INNER JOIN [Player_Info] P ON A.AccountId = P.AccountId \
        WHERE A.LoginId = ?";

	if (dbConn->Execute(query))
	{
		if (dbConn->Fetch())
		{
			WCHAR clpassword[20] = {};
			MultiByteToWideChar(CP_ACP, 0, recvPw.c_str(), -1, clpassword, _countof(clpassword));
			if (lstrcmpW(clpassword, outPassword) == 0)
			{
				outPlayerData.playerUID = outPlayerInfo.playerUID;
				GLobby->PushJob(&AuthLobby::OnLoginSuccess, session, outPlayerInfo, outPlayerData);
			}
			else
			{
				GLobby->PushJob(&AuthLobby::OnLoginFailed, session, (string)"비밀 번호 오류");
			}
		}
		else
		{
			GLobby->PushJob(&AuthLobby::OnLoginFailed, session, (string)"DB에 존재하지 않는 ID");
		}
	}
	_dbConnectionPool->Push(dbConn);
}

void DatabaseWorker::TrySignUP(shared_ptr<Session> session, string recvId, string recvPw, int playerType)
{
	DBConnection* dbConn = _dbConnectionPool->Pop();
	dbConn->Unbind();

	WCHAR loginId[50] = {};
	MultiByteToWideChar(CP_ACP, 0, recvId.c_str(), -1, loginId, _countof(loginId));
	SQLLEN idLen = 0;
	dbConn->BindParam(1, loginId, &idLen);

	WCHAR password[256] = {};
	MultiByteToWideChar(CP_ACP, 0, recvPw.c_str(), -1, password, _countof(password));
	SQLLEN pwLen = 0;
	dbConn->BindParam(2, password, &pwLen);

	dbConn->BindParam(3, loginId, &idLen); // 닉네임을 ID와 동일하게 설정

	// playerType을 위한 버퍼 (TINYINT 바인딩용)
	int32_t cType = (int32_t)playerType;
	SQLLEN typeLen = 0;
	dbConn->BindParam(4, &cType, &typeLen);

	// 트랜잭션으로 테이블 4곳에 데이터 일괄 삽입
	const WCHAR* query = L" \
        BEGIN TRAN; \
        DECLARE @AccID BIGINT, @PlayerID BIGINT; \
        INSERT INTO [User_Account] (LoginId, LoginPassword) VALUES (?, ?); \
        SET @AccID = SCOPE_IDENTITY(); \
        INSERT INTO [User_Info] (AccountId) VALUES (@AccID); \
        INSERT INTO [Player_Info] (AccountId, Name, PlayerType, Hp, Mp) VALUES (@AccID, ?, ?, 500, 100); \
        COMMIT TRAN;";

	if (dbConn->Execute(query))
	{
		cout << "[DB] 회원가입 및 캐릭터 자동생성 성공 - ID : " << recvId << endl;
	}
	else
	{
		cout << "[DB] 중복 아이디 또는 가입 오류" << endl;
	}

	_dbConnectionPool->Push(dbConn);
}


void DatabaseWorker::SavePlayerData(DB_PlayerData data)
{
	DBConnection* dbConn = _dbConnectionPool->Pop();
	dbConn->Unbind();

	SQLLEN lenList[6] = { 0 };
	dbConn->BindParam(1, &data.hp, &lenList[0]);
	dbConn->BindParam(2, &data.mp, &lenList[1]);
	dbConn->BindParam(3, &data.posX, &lenList[2]);
	dbConn->BindParam(4, &data.posY, &lenList[3]);
	dbConn->BindParam(5, &data.posZ, &lenList[4]);
	dbConn->BindParam(6, &data.playerUID, &lenList[5]);

	if (dbConn->Execute(L"UPDATE [dbo].[Player_Data] SET Hp=?, Mp=?, PosX=?, PosY=?, PosZ=? WHERE PlayerUID=?"))
	{
		// 업데이트 성공
	}

	_dbConnectionPool->Push(dbConn);
}
	


