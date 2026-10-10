#pragma once

#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace Jade::Log {

enum class Level { Trace, Debug, Info, Warn, Error, Fatal, Off };

struct Record {
  std::chrono::system_clock::time_point timestamp;
  Level level;
  std::string category;
  std::string message;
};

// Invoked synchronously, outside the output lock. Concurrent callers may invoke
// the sink concurrently. Copy the record to retain it after the callback returns.
using Sink = std::function<void(const Record&)>;

struct Config {
  Level minimumLevel = Level::Debug;
  bool console = true;
  std::filesystem::path file; // Empty disables file output; otherwise append.
  Sink sink;
};

// Process-wide configuration, independent of Application lifetime. File-open
// failure throws and preserves the current configuration. Parent dirs must exist.
void Configure(Config config = {});
void SetLevel(Level level) noexcept;
[[nodiscard]] bool Enabled(Level level) noexcept;
[[nodiscard]] std::string_view LevelName(Level level) noexcept;
void Flush() noexcept;

class Entry;

namespace Detail {
Entry MakeEntry(Level level, std::string_view message) noexcept;
Entry FormatEntry(Level level, std::string_view format, std::format_args args) noexcept;
} // namespace Detail

// An owning, move-only message. Destruction never logs. Insertion consumes it,
// including when its level is disabled; reinserting a consumed entry is a no-op.
class Entry {
public:
  Entry(Entry&& other) noexcept;
  Entry& operator=(Entry&& other) noexcept;
  Entry(const Entry&) = delete;
  Entry& operator=(const Entry&) = delete;
  ~Entry() = default;

private:
  Entry(Level level, std::string message) noexcept
    : m_Level(level), m_Message(std::move(message)) {}

  Level m_Level;
  std::string m_Message;

  friend class Logger;
  friend Entry Detail::MakeEntry(Level, std::string_view) noexcept;
  friend Entry Detail::FormatEntry(Level, std::string_view, std::format_args) noexcept;
};

// Owns its category name and shares process-wide output configuration. Logging
// failures do not escape. Fatal describes severity without terminating a process.
class Logger {
public:
  explicit Logger(std::string_view name);
  const Logger& operator<<(Entry&& entry) const noexcept;

private:
  std::string m_Name;
};

[[nodiscard]] inline Entry trace(std::string_view message) noexcept { return Detail::MakeEntry(Level::Trace, message); }
[[nodiscard]] inline Entry debug(std::string_view message) noexcept { return Detail::MakeEntry(Level::Debug, message); }
[[nodiscard]] inline Entry info(std::string_view message) noexcept { return Detail::MakeEntry(Level::Info, message); }
[[nodiscard]] inline Entry warn(std::string_view message) noexcept { return Detail::MakeEntry(Level::Warn, message); }
[[nodiscard]] inline Entry error(std::string_view message) noexcept { return Detail::MakeEntry(Level::Error, message); }
[[nodiscard]] inline Entry fatal(std::string_view message) noexcept { return Detail::MakeEntry(Level::Fatal, message); }

template<class... Args> requires (sizeof...(Args) > 0)
[[nodiscard]] Entry trace(std::format_string<Args...> format, Args&&... args) noexcept
{
  return Detail::FormatEntry(Level::Trace, format.get(), std::make_format_args(args...));
}

template<class... Args> requires (sizeof...(Args) > 0)
[[nodiscard]] Entry debug(std::format_string<Args...> format, Args&&... args) noexcept
{
  return Detail::FormatEntry(Level::Debug, format.get(), std::make_format_args(args...));
}

template<class... Args> requires (sizeof...(Args) > 0)
[[nodiscard]] Entry info(std::format_string<Args...> format, Args&&... args) noexcept
{
  return Detail::FormatEntry(Level::Info, format.get(), std::make_format_args(args...));
}

template<class... Args> requires (sizeof...(Args) > 0)
[[nodiscard]] Entry warn(std::format_string<Args...> format, Args&&... args) noexcept
{
  return Detail::FormatEntry(Level::Warn, format.get(), std::make_format_args(args...));
}

template<class... Args> requires (sizeof...(Args) > 0)
[[nodiscard]] Entry error(std::format_string<Args...> format, Args&&... args) noexcept
{
  return Detail::FormatEntry(Level::Error, format.get(), std::make_format_args(args...));
}

template<class... Args> requires (sizeof...(Args) > 0)
[[nodiscard]] Entry fatal(std::format_string<Args...> format, Args&&... args) noexcept
{
  return Detail::FormatEntry(Level::Fatal, format.get(), std::make_format_args(args...));
}

} // namespace Jade::Log
