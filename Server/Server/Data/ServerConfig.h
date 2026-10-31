#pragma once
#include <string>

// 서버 운영 설정(포트, DB 연결, 스레드 수, 로그 레벨). 게임 수치는 여기 두지 않고 DataTable(ServerData)에 둔다.
// 우선순위(뒤가 앞을 덮어씀): 코드 기본값 → ServerConfig.ini → ServerConfig.local.ini(있으면) → 환경변수 → 명령줄.
//   설정 파일: --config <경로>, 없으면 작업 폴더의 ServerConfig.ini (없으면 시작 중단)
//   환경변수: JH_DB_CONNECTION (DB 연결 문자열 — 비밀번호가 필요한 환경은 파일 대신 여기로 준다)
//   명령줄:   --port <n>
// 값이 틀리거나 모르는 키가 있으면 Load가 false를 돌려주고 서버는 시작하지 않는다.

enum class LogLevel { Debug, Info, Warn, Error };

struct ServerConfig
{
	// [Network]
	std::string bindIp = "127.0.0.1";
	int port = 8888;
	int workerThreads = 0;          // 0이면 CPU 코어 수 / 2

	// [Database]
	std::string dbConnectionString = "Driver={ODBC Driver 17 for SQL Server};Server=.\\SQLEXPRESS;Database=GameServer;Trusted_Connection=Yes;";
	int dbConnections = 4;

	// [Log] — A1.2 로거가 사용한다
	LogLevel logLevel = LogLevel::Info;

	// 값마다 어디서 왔는지(default / 파일 경로 / env / cli). 시작 로그에 함께 출력한다.
	std::string bindIpFrom = "default", portFrom = "default", workerThreadsFrom = "default";
	std::string dbConnectionStringFrom = "default", dbConnectionsFrom = "default", logLevelFrom = "default";

	// 실제로 쓸 워커 스레드 수 (0이면 CPU 코어 수 / 2, 최소 1)
	int ResolvedWorkerThreads() const;

	// 계층 순서대로 읽고 검증한다. 실패하면 false, err에 사유.
	bool Load(int argc, char* argv[], std::string& err);

	// 적용된 설정과 출처를 출력한다(연결 문자열의 PWD 값은 가린다).
	void Print() const;
};

extern ServerConfig GServerConfig;
