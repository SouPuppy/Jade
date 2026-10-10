#include <Jade/Log.hpp>

#include "Core/Log.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <ctime>
#include <iostream>
#include <iterator>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace LogTest {
struct Probe {
  int* calls;
  bool fail = false;
};
} // namespace LogTest

template<> struct std::formatter<LogTest::Probe> : std::formatter<int> {
  auto format(const LogTest::Probe& probe, std::format_context& context) const
  {
    ++*probe.calls;
    if (probe.fail) {
      throw std::runtime_error("formatter failed");
    }
    return std::formatter<int>::format(42, context);
  }
};

namespace {

class Logging : public ::testing::Test {
protected:
  const Jade::Log::Logger logger{"Test"};
  void SetUp() override { Jade::Log::Configure(); }
  void TearDown() override { Jade::Log::Configure(); }
};

class StreamCapture {
public:
  explicit StreamCapture(std::ostream& stream)
    : m_Stream(stream), m_Previous(stream.rdbuf(m_Output.rdbuf())) {}
  ~StreamCapture() { m_Stream.rdbuf(m_Previous); }
  std::string Text() const { return m_Output.str(); }
private:
  std::ostringstream m_Output;
  std::ostream& m_Stream;
  std::streambuf* m_Previous;
};

class TemporaryLog {
public:
  TemporaryLog()
    : path(std::filesystem::temp_directory_path() / std::format("jade-log-{}.txt",
        std::chrono::steady_clock::now().time_since_epoch().count())) {}
  ~TemporaryLog()
  {
    Jade::Log::Configure();
    std::error_code error;
    std::filesystem::remove(path, error);
  }
  std::string Read() const
  {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }
  std::filesystem::path path;
};

TEST_F(Logging, DefaultConsoleRoutesAndFormatPreserveRawMessages)
{
  StreamCapture output(std::cout), errors(std::cerr);
  EXPECT_TRUE(Jade::Log::Enabled(Jade::Log::Level::Debug));
  EXPECT_FALSE(Jade::Log::Enabled(Jade::Log::Level::Trace));
  logger << Jade::Log::trace("hidden");
  logger << Jade::Log::debug("debug");
  logger << Jade::Log::info("literal {braces}");
  logger << Jade::Log::warn("value={:.2f} {{ok}}", 1.25);
  logger << Jade::Log::error("failure {}", 7);
  EXPECT_EQ(output.Text().find("hidden"), std::string::npos);
  EXPECT_TRUE(std::regex_match(output.Text(), std::regex(
    R"(\d{2}:\d{2}:\d{2}\.\d{6} DEBUG \[Test\] - debug\n\d{2}:\d{2}:\d{2}\.\d{6}  INFO \[Test\] - literal \{braces\}\n\d{2}:\d{2}:\d{2}\.\d{6}  WARN \[Test\] - value=1.25 \{ok\}\n)")));
  EXPECT_EQ(output.Text().find("failure"), std::string::npos);
  EXPECT_TRUE(std::regex_match(errors.Text(), std::regex(
    R"(\d{2}:\d{2}:\d{2}\.\d{6} ERROR \[Test\] - failure 7\n)")));
  EXPECT_EQ(output.Text().find('\033'), std::string::npos);
  EXPECT_EQ(errors.Text().find('\033'), std::string::npos);
}

TEST_F(Logging, FiltersBeforeFormattingAndOffSuppressesAllLevels)
{
  std::vector<Jade::Log::Record> records;
  Jade::Log::Config config;
  config.minimumLevel = Jade::Log::Level::Info;
  config.console = false;
  config.sink = [&](const auto& record) { records.push_back(record); };
  Jade::Log::Configure(std::move(config));
  int calls = 0;
  logger << Jade::Log::debug("{}", LogTest::Probe{&calls});
  EXPECT_EQ(calls, 0);
  Jade::Log::SetLevel(Jade::Log::Level::Trace);
  logger << Jade::Log::trace("{}", LogTest::Probe{&calls});
  logger << Jade::Log::debug("debug");
  logger << Jade::Log::info("info");
  logger << Jade::Log::warn("warn");
  logger << Jade::Log::error("error");
  logger << Jade::Log::fatal("fatal");
  EXPECT_EQ(calls, 1);
  ASSERT_EQ(records.size(), 6u);
  EXPECT_EQ(records.front().message, "42");
  Jade::Log::SetLevel(Jade::Log::Level::Off);
  logger << Jade::Log::fatal("{}", LogTest::Probe{&calls});
  EXPECT_FALSE(Jade::Log::Enabled(Jade::Log::Level::Fatal));
  EXPECT_FALSE(Jade::Log::Enabled(Jade::Log::Level::Off));
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(records.size(), 6u);
}

TEST_F(Logging, RecordsOwnTheirCategoryMessageAndTimestamp)
{
  std::vector<Jade::Log::Record> records;
  Jade::Log::Config config;
  config.console = false;
  config.sink = [&](const auto& record) { records.push_back(record); };
  Jade::Log::Configure(std::move(config));
  const auto before = std::chrono::system_clock::now();
  std::optional<Jade::Log::Logger> assetsLog;
  {
    std::string category = "Assets";
    assetsLog.emplace(category);
    category = "Changed";
  }
  *assetsLog << Jade::Log::info("Loaded {} vertices", 453);
  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(records[0].category, "Assets");
  EXPECT_EQ(records[0].message, "Loaded 453 vertices");
  EXPECT_GE(records[0].timestamp, before);
  EXPECT_LE(records[0].timestamp, std::chrono::system_clock::now());
}

TEST_F(Logging, FileAppendsFlushesAndPreservesOrderAcrossReconfiguration)
{
  TemporaryLog file;
  { std::ofstream seed(file.path); seed << "existing\n"; }
  Jade::Log::Config config;
  config.console = false;
  config.file = file.path;
  Jade::Log::Configure(config);
  logger << Jade::Log::info("first");
  Jade::Log::Configure(config);
  logger << Jade::Log::info("second");
  logger << Jade::Log::error("third"); // Errors flush the file.
  auto text = file.Read();
  EXPECT_TRUE(text.starts_with("existing\n"));
  EXPECT_LT(text.find("first"), text.find("second"));
  EXPECT_NE(text.find("third"), std::string::npos);
  logger << Jade::Log::info("fourth");
  Jade::Log::Flush();
  EXPECT_NE(file.Read().find("fourth"), std::string::npos);
}

TEST_F(Logging, FailedFileConfigurationKeepsPreviousOutputAndThreshold)
{
  TemporaryLog file;
  Jade::Log::Config config;
  config.console = false;
  config.minimumLevel = Jade::Log::Level::Warn;
  config.file = file.path;
  Jade::Log::Configure(config);
  config.minimumLevel = Jade::Log::Level::Off;
  config.file = file.path / "cannot-open.txt"; // A file cannot be a parent directory.
  EXPECT_THROW(Jade::Log::Configure(config), std::runtime_error);
  logger << Jade::Log::info("hidden");
  logger << Jade::Log::warn("retained");
  Jade::Log::Flush();
  EXPECT_EQ(file.Read().find("hidden"), std::string::npos);
  EXPECT_NE(file.Read().find("retained"), std::string::npos);
}

TEST_F(Logging, SinkMayLogFlushAndReconfigureWithoutRecursingOrDeadlocking)
{
  StreamCapture output(std::cout);
  int calls = 0;
  Jade::Log::Config config;
  config.sink = [&](const auto&) {
    ++calls;
    logger << Jade::Log::info("nested");
    Jade::Log::Flush();
    Jade::Log::Configure();
  };
  Jade::Log::Configure(std::move(config));
  logger << Jade::Log::info("outer");
  logger << Jade::Log::info("after");
  EXPECT_EQ(calls, 1);
  EXPECT_NE(output.Text().find("nested"), std::string::npos);
  EXPECT_NE(output.Text().find("after"), std::string::npos);
}

TEST_F(Logging, SinkAndFormatterExceptionsDoNotEscapeOrDisableLaterLogging)
{
  int sinkCalls = 0;
  Jade::Log::Config config;
  config.console = false;
  config.sink = [&](const auto&) { ++sinkCalls; throw std::runtime_error("sink failed"); };
  Jade::Log::Configure(std::move(config));
  EXPECT_NO_THROW(logger << Jade::Log::info("first"));
  EXPECT_NO_THROW(logger << Jade::Log::info("second"));
  EXPECT_EQ(sinkCalls, 2);
  int formatCalls = 0;
  EXPECT_NO_THROW(logger << Jade::Log::info(
    "{}", (LogTest::Probe{&formatCalls, true})));
  EXPECT_EQ(formatCalls, 1);
  EXPECT_EQ(sinkCalls, 2);
  EXPECT_NO_THROW(logger << Jade::Log::info("third"));
  EXPECT_EQ(sinkCalls, 3);
}

TEST_F(Logging, ConcurrentWritersProduceCompleteUniqueFileLinesAndRecords)
{
  TemporaryLog file;
  std::mutex mutex;
  std::set<std::string> messages;
  Jade::Log::Config config;
  config.console = false;
  config.file = file.path;
  config.sink = [&](const auto& record) {
    std::lock_guard lock(mutex);
    messages.insert(record.message);
  };
  Jade::Log::Configure(std::move(config));
  const Jade::Log::Logger workerLog{"Worker"};
  std::vector<std::jthread> writers;
  for (int writer = 0; writer < 4; ++writer) {
    writers.emplace_back([writer, &workerLog] {
      for (int index = 0; index < 100; ++index) {
        workerLog << Jade::Log::info("{}:{}", writer, index);
      }
    });
  }
  writers.clear(); // Join every producer before reading the sink or flushing.
  Jade::Log::Flush();
  ASSERT_EQ(messages.size(), 400u);
  std::istringstream input(file.Read());
  std::set<std::string> written;
  for (std::string line; std::getline(input, line);) {
    const std::string prefix = " INFO [Worker] - ";
    const auto start = line.find(prefix);
    ASSERT_NE(start, std::string::npos);
    EXPECT_EQ(line.find('\033'), std::string::npos);
    EXPECT_TRUE(written.insert(line.substr(start + prefix.size())).second);
  }
  EXPECT_EQ(written, messages);
}

TEST_F(Logging, FormatsLocalTimeWithSixMicrosecondsAndAlignedColoredLevels)
{
  std::tm local{};
  local.tm_year = 126;
  local.tm_mon = 9;
  local.tm_mday = 10;
  local.tm_hour = 15;
  local.tm_min = 27;
  local.tm_sec = 7;
  local.tm_isdst = -1;
  const auto seconds = std::mktime(&local);
  ASSERT_NE(seconds, static_cast<std::time_t>(-1));
  Jade::Log::Record record{std::chrono::system_clock::from_time_t(seconds)
    + std::chrono::microseconds(591522), Jade::Log::Level::Info, "Frame", "ready"};
  EXPECT_EQ(Jade::Detail::FormatLogRecord(record, false),
    "15:27:07.591522  INFO [Frame] - ready\n");
  EXPECT_EQ(Jade::Detail::FormatLogRecord(record, true),
    "\033[90m15:27:07.591522\033[0m \033[32m INFO\033[0m [Frame] - ready\n");
  record.timestamp = std::chrono::system_clock::from_time_t(seconds)
    + std::chrono::microseconds(123);
  record.level = Jade::Log::Level::Error;
  EXPECT_EQ(Jade::Detail::FormatLogRecord(record, false),
    "15:27:07.000123 ERROR [Frame] - ready\n");
  EXPECT_EQ(Jade::Detail::FormatLogRecord(record, true),
    "\033[90m15:27:07.000123\033[0m \033[31mERROR\033[0m [Frame] - ready\n");
}

TEST_F(Logging, CreatingAndDiscardingEntriesNeverEmits)
{
  std::vector<Jade::Log::Record> records;
  Jade::Log::Config config;
  config.console = false;
  config.sink = [&](const auto& record) { records.push_back(record); };
  Jade::Log::Configure(std::move(config));
  {
    auto discarded = Jade::Log::info("discarded {}", 1);
    (void)discarded;
    EXPECT_TRUE(records.empty());
  }
  EXPECT_TRUE(records.empty());
  logger << Jade::Log::info("inserted");
  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(records.front().message, "inserted");
}

TEST_F(Logging, MovedEntriesOwnTheirPayloadAndAreConsumedOnceWithChaining)
{
  std::vector<Jade::Log::Record> records;
  Jade::Log::Config config;
  config.console = false;
  config.sink = [&](const auto& record) { records.push_back(record); };
  Jade::Log::Configure(std::move(config));
  std::string text = "retained {braces}";
  auto original = Jade::Log::info(text);
  text.clear();
  auto moved = std::move(original);
  auto assigned = Jade::Log::warn("overwritten");
  assigned = std::move(moved);
  logger << std::move(original) << std::move(moved);
  EXPECT_TRUE(records.empty());
  const auto& result = logger << std::move(assigned) << Jade::Log::info("chained");
  EXPECT_EQ(&result, &logger);
  logger << std::move(assigned);
  ASSERT_EQ(records.size(), 2u);
  EXPECT_EQ(records[0].message, "retained {braces}");
  EXPECT_EQ(records[0].level, Jade::Log::Level::Info);
  EXPECT_EQ(records[1].message, "chained");
}

TEST_F(Logging, ThresholdChangesCannotReviveFilteredOrConsumedEntries)
{
  std::vector<Jade::Log::Record> records;
  Jade::Log::Config config;
  config.minimumLevel = Jade::Log::Level::Info;
  config.console = false;
  config.sink = [&](const auto& record) { records.push_back(record); };
  Jade::Log::Configure(std::move(config));
  auto ready = Jade::Log::info("filtered at insertion");
  auto disabled = Jade::Log::debug("filtered at creation");
  Jade::Log::SetLevel(Jade::Log::Level::Error);
  logger << std::move(ready);
  Jade::Log::SetLevel(Jade::Log::Level::Trace);
  logger << std::move(ready) << std::move(disabled);
  EXPECT_TRUE(records.empty());
  logger << Jade::Log::debug("enabled now");
  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(records.front().message, "enabled now");
}

} // namespace
