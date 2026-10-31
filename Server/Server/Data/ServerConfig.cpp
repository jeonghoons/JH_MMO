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
			else if (name == "Log.Level")
			{
				if (!ParseLogLevel(e.value, cfg.logLevel)) { err = where + "Level must be Debug/Info/Warn/Error: '" + e.value + "'"; return false; }
				cfg.logLevelFrom = from;
			}
			else if (name == "Log.Dir")
			{
				cfg.logDir = e.value; cfg.logDirFrom = from;
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
	std::string cliPort;
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		if (a == "--config" || a == "--port")
		{
			if (i + 1 >= argc) { err = "command line: " + a + " needs a value"; return false; }
			(a == "--config" ? configPath : cliPort) = argv[++i];
		}
		else
		{
			err = "command line: unknown argument '" + a + "' (use --config <path>, --port <n>)";
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
	return true;
}

void ServerConfig::Print() const
{
	LOG_INFO("Config", "Network.BindIp            = {}  ({})", bindIp, bindIpFrom);
	LOG_INFO("Config", "Network.Port              = {}  ({})", port, portFrom);
	LOG_INFO("Config", "Network.WorkerThreads     = {} -> {}  ({})", workerThreads, ResolvedWorkerThreads(), workerThreadsFrom);
	LOG_INFO("Config", "Database.ConnectionString = {}  ({})", MaskSecrets(dbConnectionString), dbConnectionStringFrom);
	LOG_INFO("Config", "Database.Connections      = {}  ({})", dbConnections, dbConnectionsFrom);
	LOG_INFO("Config", "Log.Level                 = {}  ({})", LogLevelName(logLevel), logLevelFrom);
	LOG_INFO("Config", "Log.Dir                   = {}  ({})", logDir.empty() ? "(파일 출력 안 함)" : logDir, logDirFrom);
}
