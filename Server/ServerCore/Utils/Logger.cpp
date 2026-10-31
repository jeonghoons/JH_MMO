#include "CorePch.h"
#include "Utils/Logger.h"
#include <condition_variable>
#include <filesystem>
#include <cstdio>
#include <ctime>

namespace
{
	struct Record
	{
		std::chrono::system_clock::time_point time;
		DWORD threadId = 0;
		LogLevel level = LogLevel::Info;
		const char* category = "";
		std::string message;
	};

	constexpr size_t MAX_PENDING = 65536;        // 큐 상한. 넘으면 버린다
	constexpr size_t WAKE_THRESHOLD = 4096;      // 이만큼 쌓이면 출력 스레드를 바로 깨운다
	constexpr auto FLUSH_INTERVAL = std::chrono::milliseconds(50);

	std::mutex gMutex;
	std::condition_variable gCv;
	std::vector<Record> gPending;
	bool gRunning = false;
	bool gStop = false;
	std::thread gThread;
	FILE* gFile = nullptr;
	std::string gFilePath;
	std::mutex gDirectMutex;   // Init 전·Shutdown 후 직접 출력용
	std::once_flag gConsoleOnce;

	void EnsureConsoleUtf8()
	{
		std::call_once(gConsoleOnce, [] { ::SetConsoleOutputCP(CP_UTF8); });
	}

	void FormatRecord(const Record& r, std::string& out)
	{
		const std::time_t t = std::chrono::system_clock::to_time_t(r.time);
		std::tm lt{};
		localtime_s(&lt, &t);
		const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(r.time.time_since_epoch()).count() % 1000;

		std::format_to(std::back_inserter(out), "{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} {:<5} [T{}] [{}] {}\n",
			lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec, ms,
			Logger::LevelName(r.level), r.threadId, r.category, r.message);
	}

	void Output(const std::string& text)
	{
		if (text.empty()) return;
		fwrite(text.data(), 1, text.size(), stdout);
		fflush(stdout);
		if (gFile)
		{
			fwrite(text.data(), 1, text.size(), gFile);
			fflush(gFile);
		}
	}

	void WriterLoop()
	{
		std::vector<Record> batch;
		std::string text;
		long long reportedDropped = 0;

		while (true)
		{
			bool stop = false;
			{
				std::unique_lock<std::mutex> lock(gMutex);
				gCv.wait_for(lock, FLUSH_INTERVAL, [] { return gPending.size() >= WAKE_THRESHOLD || gStop; });
				batch.swap(gPending);
				stop = gStop;
			}

			text.clear();
			for (const Record& r : batch)
				FormatRecord(r, text);
			batch.clear();

			// 버린 로그가 생겼으면 한 줄로 알린다(버린 줄 자체는 복구할 수 없다).
			const long long dropped = Logger::Dropped();
			if (dropped != reportedDropped)
			{
				Record r;
				r.time = std::chrono::system_clock::now();
				r.threadId = ::GetCurrentThreadId();
				r.level = LogLevel::Warn;
				r.category = "Logger";
				r.message = std::format("로그 큐가 가득 차 {}건을 버렸습니다 (누적 {})", dropped - reportedDropped, dropped);
				FormatRecord(r, text);
				reportedDropped = dropped;
			}

			Output(text);

			if (stop)
			{
				std::lock_guard<std::mutex> lock(gMutex);
				if (gPending.empty()) break;
			}
		}
	}
}

const char* Logger::LevelName(LogLevel level)
{
	switch (level)
	{
	case LogLevel::Debug: return "DEBUG";
	case LogLevel::Info: return "INFO";
	case LogLevel::Warn: return "WARN";
	case LogLevel::Error: return "ERROR";
	}
	return "?";
}

const std::string& Logger::FilePath()
{
	return gFilePath;
}

bool Logger::Init(LogLevel minLevel, const std::string& dir, const std::string& filePrefix, std::string& err)
{
	EnsureConsoleUtf8();

	std::lock_guard<std::mutex> lock(gMutex);
	if (gRunning)
	{
		err = "logger is already initialized";
		return false;
	}

	if (!dir.empty())
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		fs::create_directories(dir, ec);
		if (ec)
		{
			err = "cannot create log directory '" + dir + "': " + ec.message();
			return false;
		}

		const std::time_t t = std::time(nullptr);
		std::tm lt{};
		localtime_s(&lt, &t);
		const std::string fileName = std::format("{}_{:04}{:02}{:02}_{:02}{:02}{:02}.log", filePrefix,
			lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
		const fs::path path = fs::path(dir) / fileName;

		if (fopen_s(&gFile, path.string().c_str(), "ab") != 0 || gFile == nullptr)
		{
			gFile = nullptr;
			err = "cannot open log file '" + path.string() + "'";
			return false;
		}
		gFilePath = path.string();
	}

	_minLevel.store((int)minLevel, std::memory_order_relaxed);
	gPending.reserve(WAKE_THRESHOLD);
	gStop = false;
	gRunning = true;
	gThread = std::thread(WriterLoop);

	// exit()로 끝날 때도 남은 로그를 쓴다. (전역 소멸자보다 먼저 실행되도록 Init에서 등록)
	static bool registered = false;
	if (!registered)
	{
		std::atexit(Logger::Shutdown);
		registered = true;
	}
	return true;
}

void Logger::Shutdown()
{
	{
		std::lock_guard<std::mutex> lock(gMutex);
		if (!gRunning) return;
		gStop = true;
		gRunning = false;   // 이후 Write는 직접 출력으로 간다
	}
	gCv.notify_one();
	if (gThread.joinable()) gThread.join();

	if (gFile)
	{
		fclose(gFile);
		gFile = nullptr;
	}
}

void Logger::Write(LogLevel level, const char* category, std::string message)
{
	Record r;
	r.time = std::chrono::system_clock::now();
	r.threadId = ::GetCurrentThreadId();
	r.level = level;
	r.category = category;
	r.message = std::move(message);

	bool queued = false;
	bool wake = false;
	{
		std::lock_guard<std::mutex> lock(gMutex);
		if (gRunning)
		{
			if (gPending.size() >= MAX_PENDING)
			{
				_dropped.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			gPending.push_back(std::move(r));
			queued = true;
			wake = (level == LogLevel::Error) || gPending.size() >= WAKE_THRESHOLD;
		}
	}

	if (queued)
	{
		if (wake) gCv.notify_one();
		return;
	}

	// 출력 스레드가 없을 때(Init 전·Shutdown 후): 바로 콘솔에 쓴다.
	EnsureConsoleUtf8();
	std::string text;
	FormatRecord(r, text);
	std::lock_guard<std::mutex> lock(gDirectMutex);
	fwrite(text.data(), 1, text.size(), stdout);
	fflush(stdout);
}
