#include "pch.h"
#include "Data/ServerConfig.h"
#include "Utils/ConfigFile.h"
#include <filesystem>
#include <cstdlib>
#include <cerrno>
#include <climits>

ServerConfig GServerConfig;

namespace
{
	namespace fs = std::filesystem;

	const char* DEFAULT_CONFIG_PATH = "ServerConfig.ini";
	const char* LOCAL_CONFIG_NAME = "ServerConfig.local.ini";

	bool ParseInt(const std::string& text, int minValue, int maxValue, int& out)
	{
		if (text.empty()) return false;
		char* end = nullptr;
		errno = 0;
		const long v = std::strtol(text.c_str(), &end, 10);
		if (*end != '\0' || errno == ERANGE || v < minValue || v > maxValue) return false;
		out = (int)v;
		return true;
	}

	bool ParseLogLevel(const std::string& text, LogLevel& out)
	{
		if (text == "Debug") { out = LogLevel::Debug; return true; }
		if (text == "Info") { out = LogLevel::Info; return true; }
		if (text == "Warn") { out = LogLevel::Warn; return true; }
		if (text == "Error") { out = LogLevel::Error; return true; }
		return false;
	}

	const char* LogLevelName(LogLevel level)
	{
		switch (level)
		{
		case LogLevel::Debug: return "Debug";
		case LogLevel::Info: return "Info";
		case LogLevel::Warn: return "Warn";
		case LogLevel::Error: return "Error";
		}
		return "?";
	}

	bool IsValidIPv4(const std::string& ip)
	{
		IN_ADDR addr;
		return ::inet_pton(AF_INET, ip.c_str(), &addr) == 1;
	}

	// 연결 문자열의 PWD=...; 값을 ***로 바꾼다 (대소문자 무시)
	std::string MaskSecrets(const std::string& conn)
	{
		std::string lower = conn;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });

		std::string result = conn;
		for (const char* key : { "pwd=", "password=" })
		{
			size_t pos = 0;
			while ((pos = lower.find(key, pos)) != std::string::npos)
			{
				const size_t valueBegin = pos + strlen(key);
				size_t valueEnd = lower.find(';', valueBegin);
				if (valueEnd == std::string::npos) valueEnd = lower.size();
				result.replace(valueBegin, valueEnd - valueBegin, std::string(valueEnd - valueBegin, '*'));
				pos = valueEnd;
			}
		}
		return result;
	}

	// 파일 하나의 값을 덮어쓴다. 모르는 키나 잘못된 값이면 false
	bool ApplyFile(ServerConfig& cfg, const ConfigFile& file, std::string& err)
	{
		for (const ConfigEntry& e : file.Entries())
		{
			const std::string where = file.Path() + " line " + std::to_string(e.line) + ": ";
			const std::string name = e.section + "." + e.key;
			const std::string& from = file.Path();

			if (name == "Network.BindIp")
			{
				if (!IsValidIPv4(e.value)) { err = where + "BindIp is not an IPv4 address: '" + e.value + "'"; return false; }
				cfg.bindIp = e.value; cfg.bindIpFrom = from;
			}
			else if (name == "Network.Port")
			{
				if (!ParseInt(e.value, 1, 65535, cfg.port)) { err = where + "Port must be 1~65535: '" + e.value + "'"; return false; }
				cfg.portFrom = from;
			}
			else if (name == "Network.WorkerThreads")
			{
				if (!ParseInt(e.value, 0, 256, cfg.workerThreads)) { err = where + "WorkerThreads must be 0~256: '" + e.value + "'"; return false; }
				cfg.workerThreadsFrom = from;
			}
			else if (name == "Network.SendFlushIntervalMs")
			{
				if (!ParseInt(e.value, 0, 200, cfg.sendFlushIntervalMs)) { err = where + "SendFlushIntervalMs must be 0~200: '" + e.value + "'"; return false; }
				cfg.sendFlushIntervalMsFrom = from;
			}
			else if (name == "Network.TimerResolutionMs")
			{
				if (!ParseInt(e.value, 0, 16, cfg.timerResolutionMs)) { err = where + "TimerResolutionMs must be 0~16: '" + e.value + "'"; return false; }
				cfg.timerResolutionMsFrom = from;
			}
			else if (name == "Network.LoginWaitTimeoutSec")
			{
				if (!ParseInt(e.value, 1, 600, cfg.loginWaitTimeoutSec)) { err = where + "LoginWaitTimeoutSec must be 1~600: '" + e.value + "'"; return false; }
				cfg.loginWaitTimeoutSecFrom = from;
			}
			else if (name == "Network.LoginCloseForceSec")
			{
				if (!ParseInt(e.value, 1, 3600, cfg.loginCloseForceSec)) { err = where + "LoginCloseForceSec must be 1~3600: '" + e.value + "'"; return false; }
				cfg.loginCloseForceSecFrom = from;
			}
			else if (name == "Database.ConnectionString")
			{
				if (e.value.empty()) { err = where + "ConnectionString is empty"; return false; }
				cfg.dbConnectionString = e.value; cfg.dbConnectionStringFrom = from;
			}
			else if (name == "Database.Connections")
			{
				if (!ParseInt(e.value, 1, 64, cfg.dbConnections)) { err = where + "Connections must be 1~64: '" + e.value + "'"; return false; }
				cfg.dbConnectionsFrom = from;
			}
			else if (name == "Database.PlayerSaveIntervalSec")
			{
				if (!ParseInt(e.value, 1, 3600, cfg.playerSaveIntervalSec)) { err = where + "PlayerSaveIntervalSec must be 1~3600: '" + e.value + "'"; return false; }
				cfg.playerSaveIntervalSecFrom = from;
			}
			else if (name == "Log.Level")
			{
				if (!ParseLogLevel(e.value, cfg.logLevel)) { err = where + "Level must be Debug/Info/Warn/Error: '" + e.value + "'"; return false; }
				cfg.logLevelFrom = from;
			}
			else if (name == "Log.Dir")
			{
				cfg.logDir = e.value; cfg.logDirFrom = from;
			}
			else if (name == "World.MapName")
			{
				// 파일 이름으로 쓰이므로 영문·숫자·밑줄만 받는다.
				bool ok = !e.value.empty() && e.value.size() <= 64;
				for (char c : e.value)
					if (!(isalnum(static_cast<unsigned char>(c)) || c == '_')) ok = false;
				if (!ok) { err = where + "MapName must be 1~64 chars of [A-Za-z0-9_]: '" + e.value + "'"; return false; }
				cfg.mapName = e.value; cfg.mapNameFrom = from;
			}
			else if (name == "Debug.DummySpawnZone")
			{
				cfg.dummyZones.clear();
				size_t begin = 0;
				while (begin < e.value.size())
				{
					size_t end = e.value.find(';', begin);
					if (end == std::string::npos) end = e.value.size();
					const std::string item = e.value.substr(begin, end - begin);
					begin = end + 1;

					ServerConfig::DummyZone z;
					char extra = 0;
					const int n = sscanf_s(item.c_str(), "%f,%f,%f,%d%c", &z.x, &z.y, &z.r, &z.count, &extra, 1);
					if ((n != 3 && n != 4) || !(z.r > 0.f) || z.count < 0)
					{
						err = where + "DummySpawnZone must be empty or x,y,r[,n];... (cm, r > 0, n >= 0): '" + item + "'";
						return false;
					}
					cfg.dummyZones.push_back(z);
				}
				cfg.dummySpawnZoneFrom = from;
			}
			else if (name == "Debug.ViewAudit")
			{
				if (e.value == "true" || e.value == "1") cfg.viewAudit = true;
				else if (e.value == "false" || e.value == "0" || e.value.empty()) cfg.viewAudit = false;
				else { err = where + "ViewAudit must be true/false: '" + e.value + "'"; return false; }
				cfg.viewAuditFrom = from;
			}
			else if (name == "Debug.DevCommands")
			{
				if (e.value == "true" || e.value == "1") cfg.devCommands = true;
				else if (e.value == "false" || e.value == "0" || e.value.empty()) cfg.devCommands = false;
				else { err = where + "DevCommands must be true/false: '" + e.value + "'"; return false; }
				cfg.devCommandsFrom = from;
			}
			else if (name == "Debug.DbItemFailEvery")
			{
				if (!ParseInt(e.value, 0, 1000000, cfg.dbItemFailEvery)) { err = where + "DbItemFailEvery must be 0~1000000: '" + e.value + "'"; return false; }
				cfg.dbItemFailEveryFrom = from;
			}
			else if (name == "Debug.DbSaveDelayMs")
			{
				if (!ParseInt(e.value, 0, 60000, cfg.dbSaveDelayMs)) { err = where + "DbSaveDelayMs must be 0~60000: '" + e.value + "'"; return false; }
				cfg.dbSaveDelayMsFrom = from;
			}
			else
			{
				err = where + "unknown key '" + name + "'";
				return false;
			}
		}
		return true;
	}

	bool LoadFile(ServerConfig& cfg, const std::string& path, std::string& err)
	{
		ConfigFile file;
		if (!file.Load(path, err)) return false;
		return ApplyFile(cfg, file, err);
	}
}

int ServerConfig::ResolvedWorkerThreads() const
{
	if (workerThreads > 0) return workerThreads;
	return std::max(1, (int)thread::hardware_concurrency() / 2);
}

bool ServerConfig::Load(int argc, char* argv[], std::string& err)
{
	// 명령줄을 먼저 훑어 설정 파일 경로를 알아내고, 덮어쓸 값은 마지막에 적용한다.
	std::string configPath = DEFAULT_CONFIG_PATH;
	std::string cliPort, cliRollDrops;
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		if (a == "--config" || a == "--port")
		{
			if (i + 1 >= argc) { err = "command line: " + a + " needs a value"; return false; }
			(a == "--config" ? configPath : cliPort) = argv[++i];
		}
		else if (a == "--roll-drops")
		{
			if (i + 1 >= argc) { err = "command line: " + a + " needs a value"; return false; }
			cliRollDrops = argv[++i];
		}
		else
		{
			err = "command line: unknown argument '" + a + "' (use --config <path>, --port <n>, --roll-drops <kills>)";
			return false;
		}
	}

	// 1) 설정 파일 (필수)
	if (!fs::exists(configPath))
	{
		err = configPath + ": config file not found (working directory: " + fs::current_path().string() + ")";
		return false;
	}
	if (!LoadFile(*this, configPath, err)) return false;

	// 2) 같은 폴더의 개인 PC용 덮어쓰기 파일 (선택, 커밋하지 않음)
	const fs::path localPath = fs::path(configPath).parent_path() / LOCAL_CONFIG_NAME;
	if (fs::exists(localPath))
	{
		if (!LoadFile(*this, localPath.string(), err)) return false;
	}

	// 3) 환경변수
	char* env = nullptr;
	size_t envLen = 0;
	if (_dupenv_s(&env, &envLen, "JH_DB_CONNECTION") == 0 && env != nullptr)
	{
		const std::string value = env;
		free(env);
		if (value.empty()) { err = "env JH_DB_CONNECTION is empty"; return false; }
		dbConnectionString = value;
		dbConnectionStringFrom = "env JH_DB_CONNECTION";
	}

	// 4) 명령줄
	if (!cliPort.empty())
	{
		if (!ParseInt(cliPort, 1, 65535, port)) { err = "command line: --port must be 1~65535: '" + cliPort + "'"; return false; }
		portFrom = "cli --port";
	}
	if (!cliRollDrops.empty())
	{
		if (!ParseInt(cliRollDrops, 1, 10000000, rollDropsKills)) { err = "command line: --roll-drops must be 1~10000000: '" + cliRollDrops + "'"; return false; }
	}
	return true;
}

void ServerConfig::Print() const
{
	LOG_INFO("Config", "Network.BindIp            = {}  ({})", bindIp, bindIpFrom);
	LOG_INFO("Config", "Network.Port              = {}  ({})", port, portFrom);
	LOG_INFO("Config", "Network.WorkerThreads     = {} -> {}  ({})", workerThreads, ResolvedWorkerThreads(), workerThreadsFrom);
	LOG_INFO("Config", "Network.SendFlushIntervalMs = {}  ({})", sendFlushIntervalMs, sendFlushIntervalMsFrom);
	LOG_INFO("Config", "Network.TimerResolutionMs = {}  ({})", timerResolutionMs, timerResolutionMsFrom);
	LOG_INFO("Config", "Network.LoginWaitTimeoutSec = {}  ({})", loginWaitTimeoutSec, loginWaitTimeoutSecFrom);
	LOG_INFO("Config", "Network.LoginCloseForceSec = {}  ({})", loginCloseForceSec, loginCloseForceSecFrom);
	LOG_INFO("Config", "Database.ConnectionString = {}  ({})", MaskSecrets(dbConnectionString), dbConnectionStringFrom);
	LOG_INFO("Config", "Database.Connections      = {}  ({})", dbConnections, dbConnectionsFrom);
	LOG_INFO("Config", "Database.PlayerSaveIntervalSec = {}  ({})", playerSaveIntervalSec, playerSaveIntervalSecFrom);
	LOG_INFO("Config", "Log.Level                 = {}  ({})", LogLevelName(logLevel), logLevelFrom);
	LOG_INFO("Config", "Log.Dir                   = {}  ({})", logDir.empty() ? "(파일 출력 안 함)" : logDir, logDirFrom);
	LOG_INFO("Config", "World.MapName             = {}  ({})", mapName, mapNameFrom);
	if (dummyZones.empty())
		LOG_INFO("Config", "Debug.DummySpawnZone      = (끔: 맵 전체 무작위)  ({})", dummySpawnZoneFrom);
	for (const DummyZone& z : dummyZones)
		LOG_INFO("Config", "Debug.DummySpawnZone      = {},{},{} x {}  ({})", z.x, z.y, z.r, z.count == 0 ? std::string("나머지") : std::to_string(z.count), dummySpawnZoneFrom);
	LOG_INFO("Config", "Debug.ViewAudit           = {}  ({})", viewAudit, viewAuditFrom);
	LOG_INFO("Config", "Debug.DbSaveDelayMs       = {}  ({})", dbSaveDelayMs, dbSaveDelayMsFrom);
	LOG_INFO("Config", "Debug.DevCommands         = {}  ({})", devCommands, devCommandsFrom);
	LOG_INFO("Config", "Debug.DbItemFailEvery     = {}  ({})", dbItemFailEvery, dbItemFailEveryFrom);
}
