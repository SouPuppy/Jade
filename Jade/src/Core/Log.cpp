#include "Log.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace Jade::Detail {
namespace {

std::string_view LevelColor(Log::Level level) noexcept
{
  switch (level) {
  case Log::Level::Trace: return "\033[2;37m";
  case Log::Level::Debug: return "\033[36m";
  case Log::Level::Info: return "\033[32m";
  case Log::Level::Warn: return "\033[33m";
  case Log::Level::Error: return "\033[31m";
  case Log::Level::Fatal: return "\033[1;31m";
  case Log::Level::Off: return "\033[0m";
  }
  return "\033[0m";
}

} // namespace

std::string FormatLogRecord(const Log::Record& record, bool color)
{
  const auto seconds = std::chrono::floor<std::chrono::seconds>(record.timestamp);
  const auto time = std::chrono::system_clock::to_time_t(seconds);
  std::tm local{};
#if defined(_WIN32)
  if (localtime_s(&local, &time) != 0) {
#else
  if (localtime_r(&time, &local) == nullptr) {
#endif
    throw std::runtime_error("Failed to convert log timestamp to local time.");
  }
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
    record.timestamp - seconds).count();
  const auto timestamp = std::format("{:02}:{:02}:{:02}.{:06}",
    local.tm_hour, local.tm_min, local.tm_sec, micros);
  if (color) {
    return std::format("\033[90m{}\033[0m {}{:>5}\033[0m [{}] - {}\n",
      timestamp, LevelColor(record.level), Log::LevelName(record.level),
      record.category, record.message);
  }
  return std::format("{} {:>5} [{}] - {}\n", timestamp,
    Log::LevelName(record.level), record.category, record.message);
}

} // namespace Jade::Detail

namespace Jade::Log {
namespace {

struct State {
  std::atomic<Level> minimumLevel{Level::Debug};
  std::mutex mutex;
  bool console = true;
  std::ofstream file;
  std::shared_ptr<const Sink> sink;
};

State& CurrentState()
{
  static State state;
  return state;
}

thread_local bool insideSink = false;

// Remember the native streams before any consumer replaces their stream buffers.
const auto* stdoutBuffer = std::cout.rdbuf();
const auto* stderrBuffer = std::cerr.rdbuf();

bool ConsoleColorEnabled(bool errors) noexcept
{
#if defined(_WIN32)
  char terminalName[8]{};
  if (GetEnvironmentVariableA("NO_COLOR", nullptr, 0) > 1
      || (GetEnvironmentVariableA("TERM", terminalName, sizeof(terminalName)) == 4
        && std::string_view(terminalName) == "dumb")) {
#else
  const auto* noColor = std::getenv("NO_COLOR");
  const auto* term = std::getenv("TERM");
  if ((noColor && *noColor) || (term && std::string_view(term) == "dumb")) {
#endif
    return false;
  }
  if ((errors ? std::cerr.rdbuf() : std::cout.rdbuf())
      != (errors ? stderrBuffer : stdoutBuffer)) {
    return false;
  }
#if defined(_WIN32)
  const auto handle = GetStdHandle(errors ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (!GetConsoleMode(handle, &mode)) {
    return false; // Files, pipes and other non-console handles have no console mode.
  }
  return (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0
    || SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
  return isatty(fileno(errors ? stderr : stdout)) != 0;
#endif
}

void ReportFailure() noexcept
{
  try {
    const auto line = Jade::Detail::FormatLogRecord({std::chrono::system_clock::now(),
      Level::Error, "Log", "Failed to write a log record or invoke its sink."},
      ConsoleColorEnabled(true));
    std::fputs(line.c_str(), stderr);
  } catch (...) {
    std::fputs("ERROR [Log] - Failed to write a log record or invoke its sink.\n", stderr);
  }
}

} // namespace

void Configure(Config config)
{
  std::ofstream file;
  if (!config.file.empty()) {
    file.open(config.file, std::ios::out | std::ios::app);
    if (!file.is_open()) {
      throw std::runtime_error("Failed to open log file: " + config.file.string());
    }
    file.exceptions(std::ios::badbit | std::ios::failbit);
  }
  auto sink = config.sink ? std::make_shared<const Sink>(std::move(config.sink)) : nullptr;
  auto& state = CurrentState();
  {
    std::lock_guard lock(state.mutex);
    if (state.file.is_open()) {
      state.file.flush();
    }
    state.file.swap(file);
    state.sink.swap(sink);
    state.console = config.console;
    state.minimumLevel.store(config.minimumLevel, std::memory_order_relaxed);
  }
  // Retire the old file and sink outside the lock; a sink destructor may log.
}

void SetLevel(Level level) noexcept
{
  CurrentState().minimumLevel.store(level, std::memory_order_relaxed);
}

bool Enabled(Level level) noexcept
{
  return level != Level::Off
    && level >= CurrentState().minimumLevel.load(std::memory_order_relaxed);
}

std::string_view LevelName(Level level) noexcept
{
  switch (level) {
  case Level::Trace: return "TRACE";
  case Level::Debug: return "DEBUG";
  case Level::Info: return "INFO";
  case Level::Warn: return "WARN";
  case Level::Error: return "ERROR";
  case Level::Fatal: return "FATAL";
  case Level::Off: return "OFF";
  }
  return "UNKNOWN";
}

namespace {

void Emit(Level level, std::string_view category, std::string_view message) noexcept
{
  if (!Enabled(level)) {
    return;
  }
  try {
    const Record record{std::chrono::system_clock::now(), level,
      std::string(category), std::string(message)};
    auto& state = CurrentState();
    std::shared_ptr<const Sink> sink;
    {
      std::lock_guard lock(state.mutex);
      if (!Enabled(level)) {
        return;
      }
      sink = state.sink;
      try {
        if (state.console || state.file.is_open()) {
          const auto line = Jade::Detail::FormatLogRecord(record, false);
          if (state.console) {
            const bool errors = level >= Level::Error;
            auto& output = errors ? std::cerr : std::cout;
            output << (ConsoleColorEnabled(errors)
              ? Jade::Detail::FormatLogRecord(record, true) : line);
          }
          if (state.file.is_open()) {
            state.file << line;
            if (level >= Level::Error) {
              state.file.flush();
            }
          }
        }
      } catch (...) {
        ReportFailure();
      }
    }
    // Nested logging still reaches console/file, but cannot recurse into the sink.
    if (sink && !insideSink) {
      struct SinkGuard {
        SinkGuard() { insideSink = true; }
        ~SinkGuard() { insideSink = false; }
      } guard;
      (*sink)(record);
    }
  } catch (...) {
    ReportFailure();
  }
}

} // namespace

Entry::Entry(Entry&& other) noexcept
  : m_Level(std::exchange(other.m_Level, Level::Off)),
    m_Message(std::move(other.m_Message)) {}

Entry& Entry::operator=(Entry&& other) noexcept
{
  if (this != &other) {
    m_Level = std::exchange(other.m_Level, Level::Off);
    m_Message = std::move(other.m_Message);
  }
  return *this;
}

Logger::Logger(std::string_view name) : m_Name(name) {}

const Logger& Logger::operator<<(Entry&& entry) const noexcept
{
  const auto level = std::exchange(entry.m_Level, Level::Off);
  Emit(level, m_Name, entry.m_Message);
  return *this;
}

Entry Detail::MakeEntry(Level level, std::string_view message) noexcept
{
  if (!Enabled(level)) {
    return Entry(Level::Off, {});
  }
  try {
    return Entry(level, std::string(message));
  } catch (...) {
    ReportFailure();
    return Entry(Level::Off, {});
  }
}

Entry Detail::FormatEntry(Level level, std::string_view format, std::format_args args) noexcept
{
  if (!Enabled(level)) {
    return Entry(Level::Off, {});
  }
  try {
    return Entry(level, std::vformat(format, args));
  } catch (...) {
    ReportFailure();
    return Entry(Level::Off, {});
  }
}

void Flush() noexcept
{
  try {
    auto& state = CurrentState();
    std::lock_guard lock(state.mutex);
    std::cout.flush();
    std::cerr.flush();
    if (state.file.is_open()) {
      state.file.flush();
    }
  } catch (...) {
    ReportFailure();
  }
}

} // namespace Jade::Log
