#pragma once
#include <string>
#include <format>
#include <atomic>

// 레벨/타임스탬프 비동기 로거. 게임 스레드는 레벨 확인 → 문자열 포맷 → 큐에 넣기까지만 하고,
// 콘솔·파일 출력은 전용 스레드가 모아서 한다. 큐가 가득 차면 게임 스레드를 막지 않고 버리며 버린 개수를 센다.
// 출력 형식: 2026-10-31 14:03:22.123 INFO  [T1234] [Room] 메시지   (UTF-8, 콘솔도 UTF-8 코드페이지로 맞춘다)
// Init 전·Shutdown 후의 로그는 큐를 거치지 않고 콘솔에 바로 쓴다(설정 오류 같은 시작 단계 메시지).
//
// 사용: LOG_INFO("Room", "Player[{}] Enter", playerId);   — std::format 문법, 걸러지는 레벨은 포맷 비용도 들지 않는다.

enum class LogLevel { Debug, Info, Warn, Error };

class Logger
{
public:
	// minLevel 미만은 버린다. dir이 비어 있지 않으면 dir/<filePrefix>_<시작시각>.log 에도 쓴다(폴더는 만든다).
	// 실패하면 false, err에 사유. 성공하면 프로세스 종료(exit) 때 남은 로그를 자동으로 flush한다.
	static bool Init(LogLevel minLevel, const std::string& dir, const std::string& filePrefix, std::string& err);

	// 큐에 남은 로그를 모두 쓰고 출력 스레드를 멈춘다. 여러 번 불러도 된다.
	static void Shutdown();

	static bool IsEnabled(LogLevel level) { return (int)level >= _minLevel.load(std::memory_order_relaxed); }
	static void Write(LogLevel level, const char* category, std::string message);

	// 큐가 가득 차 버린 로그 수(누적)
	static long long Dropped() { return _dropped.load(std::memory_order_relaxed); }
	static const std::string& FilePath();
	static const char* LevelName(LogLevel level);

private:
	inline static std::atomic<int> _minLevel{ (int)LogLevel::Info };
	inline static std::atomic<long long> _dropped{ 0 };
};

#define LOG_WRITE(level, category, ...) \
	do { if (Logger::IsEnabled(level)) Logger::Write(level, category, std::format(__VA_ARGS__)); } while (0)

#define LOG_DEBUG(category, ...) LOG_WRITE(LogLevel::Debug, category, __VA_ARGS__)
#define LOG_INFO(category, ...)  LOG_WRITE(LogLevel::Info, category, __VA_ARGS__)
#define LOG_WARN(category, ...)  LOG_WRITE(LogLevel::Warn, category, __VA_ARGS__)
#define LOG_ERROR(category, ...) LOG_WRITE(LogLevel::Error, category, __VA_ARGS__)
