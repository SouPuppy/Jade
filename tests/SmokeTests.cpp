#include <Jade/Renderer/RayTracing/RayTracing.hpp>
#include "TestScene.hpp"
#include <Jade/Application.hpp>
#include <Jade/Color.hpp>
#include <Jade/Core/Transform.hpp>
#include <Jade/Math/Vector.hpp>
#include <Jade/Log.hpp>

#include "internal/Core/FrameClock.hpp"
#include "internal/Platform/Window.hpp"

#include <gtest/gtest.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class LogCapture final {
public:
  explicit LogCapture(Jade::Log::Level level = Jade::Log::Level::Debug)
  {
    Jade::Log::Config config;
    config.minimumLevel = level;
    config.console = false;
    config.sink = [this](const auto &record) { m_Records.push_back(record); };
    Jade::Log::Configure(std::move(config));
  }
  ~LogCapture() { Jade::Log::Configure(); }

  LogCapture(const LogCapture &) = delete;
  LogCapture &operator=(const LogCapture &) = delete;

  std::vector<Jade::Log::Record> Records(std::string_view category, Jade::Log::Level level) const
  {
    std::vector<Jade::Log::Record> result;
    for (const auto &record : m_Records) {
      if (record.category == category && record.level == level)
        result.push_back(record);
    }
    return result;
  }
  void Reset() { m_Records.clear(); }

private:
  std::vector<Jade::Log::Record> m_Records;
};

void CheckSummary(const Jade::Log::Record &record)
{
  const std::regex pattern(
      R"(fps=([0-9.]+) interval_avg=([0-9.]+)ms interval_max=([0-9.]+)ms work_avg=([0-9.]+)ms work_max=([0-9.]+)ms)");
  std::smatch values;
  ASSERT_TRUE(std::regex_match(record.message, values, pattern));
  EXPECT_GT(std::stod(values[1]), 0.0);
  EXPECT_GT(std::stod(values[2]), 0.0);
  EXPECT_GE(std::stod(values[3]), std::stod(values[2]));
  EXPECT_GE(std::stod(values[4]), 0.0);
  EXPECT_GE(std::stod(values[5]), std::stod(values[4]));
}

Jade::ApplicationConfig HiddenApplication()
{
  Jade::ApplicationConfig config;
  config.window.visible = false;
  config.window.width = 160;
  config.window.height = 120;
  return config;
}

} // namespace

TEST(Application, ThreeClearFrames)
{
  LogCapture capture(Jade::Log::Level::Trace);
  auto config = HiddenApplication();
  config.logPerformance = false;
  Jade::Application app(config);
  EXPECT_NO_THROW(app.Run({3}));
  EXPECT_TRUE(capture.Records("Performance", Jade::Log::Level::Debug).empty());
  EXPECT_TRUE(capture.Records("Frame", Jade::Log::Level::Trace).empty());
}

TEST(Application, RejectsInvalidConfigurationBeforeCreatingAWindow)
{
  auto config = HiddenApplication();
  config.frameLimits = 0;
  EXPECT_THROW((Jade::Application{config}), std::invalid_argument);
  config.frameLimits = 24;
  config.window.width = 0;
  EXPECT_THROW((Jade::Application{config}), std::invalid_argument);
}

TEST(FrameClock, UsesAbsoluteDeadlinesAndSkipsMissedSlots)
{
  using FrameClock = Jade::Detail::FrameClock;
  using namespace std::chrono_literals;
  const FrameClock::TimePoint start{};
  FrameClock clock(24, start);
  EXPECT_EQ(clock.NextDeadline(), start);
  EXPECT_DOUBLE_EQ(clock.FinishFrame(start + 5ms).fps, 0.0);
  const auto firstDeadline = clock.NextDeadline();
  const auto period = firstDeadline - start;
  EXPECT_NEAR(std::chrono::duration<double>(period).count(), 1.0 / 24.0, 1e-7);
  clock.FinishFrame(firstDeadline + 5ms);
  EXPECT_EQ(clock.NextDeadline(), start + 2 * period);

  clock.FinishFrame(start + 150ms);
  EXPECT_GT(clock.NextDeadline(), start + 150ms);
  EXPECT_EQ(clock.NextDeadline(), start + 4 * period);
  clock.Resume(start + 5s);
  EXPECT_EQ(clock.NextDeadline(), start + 5s);
  EXPECT_EQ(clock.FinishFrame(start + 5s).frame, 4u);
  EXPECT_THROW((FrameClock{0, start}), std::invalid_argument);
}

TEST(FrameClock, MeasuresActualFPSAndResetsSecondCounters)
{
  using FrameClock = Jade::Detail::FrameClock;
  using namespace std::chrono_literals;
  const FrameClock::TimePoint start{};
  FrameClock clock(24, start);
  const auto first = clock.FinishFrame(start + 990ms);
  EXPECT_EQ(first.second, 1u);
  EXPECT_EQ(first.frameInSecond, 1u);
  EXPECT_DOUBLE_EQ(first.fps, 0.0);

  const auto second = clock.FinishFrame(start + 1010ms);
  EXPECT_EQ(second.second, 2u);
  EXPECT_EQ(second.frame, 2u);
  EXPECT_EQ(second.frameInSecond, 1u);
  EXPECT_NEAR(second.fps, 50.0, 1e-6);
  const auto third = clock.FinishFrame(start + 1100ms);
  EXPECT_EQ(third.frameInSecond, 2u);
  EXPECT_NEAR(third.fps, 2.0 / 0.11, 1e-6);
  EXPECT_DOUBLE_EQ(clock.FinishFrame(start + 2200ms).fps, 0.0);

  FrameClock restarted(24, start + 3s);
  const auto reset = restarted.FinishFrame(start + 3s);
  EXPECT_EQ(reset.frame, 1u);
  EXPECT_EQ(reset.second, 1u);
  EXPECT_EQ(reset.frameInSecond, 1u);
  EXPECT_DOUBLE_EQ(reset.elapsed, 0.0);
}

TEST(Application, KeepsComponentReferencesStableAndLogsPacedFramesAcrossRuns)
{
  LogCapture capture;
  auto config = HiddenApplication();
  Jade::Application app(config);
  const auto scene = MakeTestScene(app);
  auto entity = scene->create();
  auto &pose = entity.add<Jade::Transform>();
  const auto *address = &pose;
  for (int index = 0; index < 64; ++index) {
    scene->create().add<Jade::Transform>();
  }
  EXPECT_EQ(&entity.get<Jade::Transform>(), address);

  const auto ready = capture.Records("Application", Jade::Log::Level::Info);
  ASSERT_EQ(ready.size(), 1u);
  EXPECT_NE(ready[0].message.find("target_fps=24 vsync=off"), std::string::npos);
  capture.Reset();
  std::vector<std::uint64_t> frames;
  app.on<Jade::FrameUpdate>([&frames](const auto &event) { frames.push_back(event.frame); });
  const auto start = std::chrono::steady_clock::now();
  ASSERT_NO_THROW(app.Run({35}));
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  EXPECT_GE(elapsed, (34.0 / 24.0) * 0.85);
  ASSERT_EQ(frames.size(), 35u);
  for (std::size_t index = 0; index < frames.size(); ++index) {
    EXPECT_EQ(frames[index], index + 1);
  }
  const auto summaries = capture.Records("Performance", Jade::Log::Level::Debug);
  ASSERT_FALSE(summaries.empty());
  EXPECT_LT(summaries.size(), frames.size());
  for (std::size_t index = 0; index < summaries.size(); ++index) {
    CheckSummary(summaries[index]);
    if (index > 0) {
      EXPECT_GE(
          std::chrono::duration<double>(summaries[index].timestamp - summaries[index - 1].timestamp).count(),
          0.9);
    }
  }
  EXPECT_TRUE(capture.Records("Frame", Jade::Log::Level::Trace).empty());
  const auto lifecycle = capture.Records("Application", Jade::Log::Level::Info);
  ASSERT_EQ(lifecycle.size(), 2u);
  EXPECT_EQ(lifecycle[0].message, "Run started");
  EXPECT_TRUE(lifecycle[1].message.starts_with("Run stopped: frames=35 "));
  EXPECT_TRUE(lifecycle[1].message.ends_with("reason=frame_limit"));

  capture.Reset();
  frames.clear();
  pose.position = {2, 2, 0};
  ASSERT_NO_THROW(app.Run({2}));
  EXPECT_EQ(frames, (std::vector<std::uint64_t>{1, 2}));
  EXPECT_TRUE(capture.Records("Performance", Jade::Log::Level::Debug).empty());
  const auto restarted = capture.Records("Application", Jade::Log::Level::Info);
  ASSERT_EQ(restarted.size(), 2u);
  EXPECT_TRUE(restarted.back().message.starts_with("Run stopped: frames=2 "));
  EXPECT_EQ(pose.position, (Jade::Vec3{2, 2, 0}));
}

TEST(Application, PerFrameTraceRequiresExplicitVerbosity)
{
  LogCapture capture(Jade::Log::Level::Trace);
  Jade::Application app(HiddenApplication());
  app.Run({3});
  const auto records = capture.Records("Frame", Jade::Log::Level::Trace);
  ASSERT_EQ(records.size(), 3u);
  for (std::size_t index = 0; index < records.size(); ++index) {
    EXPECT_TRUE(records[index].message.starts_with(std::format("frame={} interval=", index + 1)));
    EXPECT_NE(records[index].message.find(" work="), std::string::npos);
  }
  EXPECT_TRUE(capture.Records("Performance", Jade::Log::Level::Debug).empty());
}

TEST(Application, RecordsFailureAndCanRunAgainAfterACallbackException)
{
  LogCapture capture;
  Jade::Application app(HiddenApplication());
  bool fail = true;
  app.on<Jade::FrameUpdate>([&fail] {
    if (fail)
      throw std::runtime_error("callback failed");
  });
  EXPECT_THROW(app.Run({1}), std::runtime_error);
  const auto errors = capture.Records("Application", Jade::Log::Level::Error);
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_TRUE(errors[0].message.starts_with("Run aborted by exception: frames=0 "));
  fail = false;
  EXPECT_NO_THROW(app.Run({1}));
  const auto lifecycle = capture.Records("Application", Jade::Log::Level::Info);
  ASSERT_EQ(lifecycle.size(), 4u);
  EXPECT_TRUE(lifecycle.back().message.ends_with("reason=frame_limit"));
}

TEST(Application, PreservesExplicitLogThresholdAndRecordsRequestedStop)
{
  LogCapture capture(Jade::Log::Level::Info);
  Jade::Application app(HiddenApplication());
  EXPECT_FALSE(Jade::Log::Enabled(Jade::Log::Level::Debug));
  app.on<Jade::FrameUpdate>([&app] { app.stop(); });
  EXPECT_NO_THROW(app.Run({3}));
  const auto lifecycle = capture.Records("Application", Jade::Log::Level::Info);
  ASSERT_EQ(lifecycle.size(), 3u);
  EXPECT_EQ(lifecycle[1].message, "Run started");
  EXPECT_TRUE(lifecycle.back().message.starts_with("Run stopped: frames=0 "));
  EXPECT_TRUE(lifecycle.back().message.ends_with("reason=requested"));
  EXPECT_TRUE(capture.Records("Frame", Jade::Log::Level::Trace).empty());
  EXPECT_TRUE(capture.Records("Performance", Jade::Log::Level::Debug).empty());
}

TEST(Assets, LogsLoadsOnceAndReportsErrorsWhilePreservingExceptions)
{
  LogCapture capture;
  Jade::Assets assets;
  const auto directory = std::filesystem::path(JADE_TEST_ASSET_DIR);
  const auto mesh = assets.load<Jade::Mesh>(directory / "bunny.obj");
  EXPECT_EQ(assets.load<Jade::Mesh>(directory / "." / "bunny.obj"), mesh);
  const auto loads = capture.Records("Assets", Jade::Log::Level::Debug);
  ASSERT_EQ(loads.size(), 1u);
  EXPECT_TRUE(loads[0].message.starts_with("Loaded mesh: path="));
  EXPECT_NE(loads[0].message.find("vertices=453 triangles=948 duration="), std::string::npos);
  EXPECT_THROW(assets.load<Jade::Mesh>(directory / "missing.obj"), std::runtime_error);
  const auto errors = capture.Records("Assets", Jade::Log::Level::Error);
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_NE(errors[0].message.find("missing.obj"), std::string::npos);
  EXPECT_NE(errors[0].message.find("reason="), std::string::npos);
  EXPECT_EQ(assets.load<Jade::Mesh>(directory / "bunny.obj"), mesh);
}
