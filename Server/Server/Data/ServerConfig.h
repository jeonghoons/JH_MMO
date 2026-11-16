#pragma once
#include <string>

// 서버 운영 설정(포트, DB 연결, 스레드 수, 로그 레벨). 게임 수치는 여기 두지 않고 DataTable(ServerData)에 둔다.
// 우선순위(뒤가 앞을 덮어씀): 코드 기본값 → ServerConfig.ini → ServerConfig.local.ini(있으면) → 환경변수 → 명령줄.
//   설정 파일: --config <경로>, 없으면 작업 폴더의 ServerConfig.ini (없으면 시작 중단)
//   환경변수: JH_DB_CONNECTION (DB 연결 문자열 — 비밀번호가 필요한 환경은 파일 대신 여기로 준다)
//   명령줄:   --port <n>, --roll-drops <kills>(시험용 — DataTable을 읽고 드랍 롤 빈도만 로그에 남긴 뒤 종료, A3.2)
// 값이 틀리거나 모르는 키가 있으면 Load가 false를 돌려주고 서버는 시작하지 않는다.

struct ServerConfig
{
	// [Network]
	std::string bindIp = "127.0.0.1";
	int port = 8888;
	int workerThreads = 0;          // 0이면 CPU 코어 수 / 2
	// 중복 접속 차단(A2.7, D-158)
	int loginWaitTimeoutSec = 10;   // 같은 캐릭터의 이전 접속 정리(퇴장·마지막 저장)를 기다리는 시간. 넘으면 LOGIN_RESULT_BUSY
	int loginCloseForceSec = 30;    // 정리가 끝났다는 신호가 이 시간 안에 오지 않으면 오류 로그를 남기고 접속 목록에서 강제로 뺀다

	// [Database]
	std::string dbConnectionString = "Driver={ODBC Driver 17 for SQL Server};Server=.\\SQLEXPRESS;Database=GameServer;Trusted_Connection=Yes;";
	int dbConnections = 4;
	int playerSaveIntervalSec = 60; // 진행 상황(레벨·경험치·HP·골드) 주기 저장 간격(A2.5). 값이 달라진 플레이어만 저장한다

	// [Log] — Logger(ServerCore Utils/Logger)에 넘긴다
	LogLevel logLevel = LogLevel::Info;
	std::string logDir = "Logs";    // 비우면 파일에 쓰지 않는다(콘솔만)

	// [Debug] — 부하·검증 시험용. 기본은 모두 꺼짐
	// 더미(봇) 로그인을 이 원들 안에 배치(핫존·시야 경계 시험). 비면 맵 전체 무작위.
	// 형식: x,y,r[,n];x,y,r[,n];... — 들어오는 순서대로 앞 구역부터 n명씩 채우고, n이 없는 구역은 나머지 전부
	struct DummyZone { float x = 0, y = 0, r = 0; int count = 0; };   // count 0 = 제한 없음
	std::vector<DummyZone> dummyZones;
	bool viewAudit = false;         // 방마다 1초에 한 번 시야 목록을 실제 거리(WorldTable.ViewRadiusCm/ViewHysteresisCm)와 비교
	bool devCommands = false;       // 개발 지급 명령(CS_DEV_GIVE)을 받는다(A3.3, D-152). 꺼져 있으면 NOT_ALLOWED
	int dbItemFailEvery = 0;        // 0보다 크면 아이템 트랜잭션을 이 횟수마다 한 번 일부러 롤백한다(DB 실패 시험, D-162)
	int dbSaveDelayMs = 0;          // 아이템 트랜잭션·진행 상황 저장을 실행하기 전에 이만큼 멈춘다(빠른 재접속·중복 접속 시험, A2.7). DB 작업 스레드를 막으므로 시험에만 쓴다

	// 명령줄 --roll-drops <kills>: 0보다 크면 DataTable 로드 뒤 몬스터마다 그 횟수만큼 드랍을 굴려 빈도를 찍고 종료한다(DB·포트를 열지 않음)
	int rollDropsKills = 0;

	// 값마다 어디서 왔는지(default / 파일 경로 / env / cli). 시작 로그에 함께 출력한다.
	std::string bindIpFrom = "default", portFrom = "default", workerThreadsFrom = "default";
	std::string dbConnectionStringFrom = "default", dbConnectionsFrom = "default", playerSaveIntervalSecFrom = "default", logLevelFrom = "default", logDirFrom = "default";
	std::string dummySpawnZoneFrom = "default", viewAuditFrom = "default", dbSaveDelayMsFrom = "default";
	std::string loginWaitTimeoutSecFrom = "default", loginCloseForceSecFrom = "default";
	std::string devCommandsFrom = "default", dbItemFailEveryFrom = "default";

	// 실제로 쓸 워커 스레드 수 (0이면 CPU 코어 수 / 2, 최소 1)
	int ResolvedWorkerThreads() const;

	// 계층 순서대로 읽고 검증한다. 실패하면 false, err에 사유.
	bool Load(int argc, char* argv[], std::string& err);

	// 적용된 설정과 출처를 출력한다(연결 문자열의 PWD 값은 가린다).
	void Print() const;
};

extern ServerConfig GServerConfig;
