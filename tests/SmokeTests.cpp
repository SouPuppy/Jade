#include <Jade/Application.hpp>
#include <Jade/Color.hpp>
#include <Jade/Core/Transform.hpp>
#include <Jade/Math/Vector.hpp>

#include "internal/Core/FrameClock.hpp"
#include "internal/Platform/Window.hpp"
#include "internal/Renderer/Renderer.hpp"

#include <gtest/gtest.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class LogCapture final {
public:
  LogCapture() : m_Previous(std::cout.rdbuf(m_Output.rdbuf())) {}
  ~LogCapture() { std::cout.rdbuf(m_Previous); }

  LogCapture(const LogCapture&) = delete;
  LogCapture& operator=(const LogCapture&) = delete;

  std::string Text() const { return m_Output.str(); }
  void Reset() { m_Output.str(""); m_Output.clear(); }

private:
  std::ostringstream m_Output;
  std::streambuf* m_Previous;
};

std::vector<Jade::Detail::FrameStats> ParseFrames(const std::string& output)
{
  const std::regex pattern(
    R"(\[Jade\] elapsed=([0-9.]+)s second=([0-9]+) frame=([0-9]+) frame_in_second=([0-9]+) fps=([0-9.]+) frame_limits=24)");
  std::vector<Jade::Detail::FrameStats> frames;
  for (auto it = std::sregex_iterator(output.begin(), output.end(), pattern);
      it != std::sregex_iterator(); ++it) {
    frames.push_back({std::stod((*it)[1]), std::stoull((*it)[2]),
      std::stoull((*it)[3]), std::stoull((*it)[4]), std::stod((*it)[5])});
  }
  return frames;
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
  auto config = HiddenApplication();
  config.logFrames = false;
  Jade::Application app(config);
  EXPECT_NO_THROW(app.Run({ 3, true }));
}

TEST(Application, RejectsInvalidConfigurationBeforeCreatingAWindow)
{
  auto config = HiddenApplication();
  config.frameLimits = 0;
  EXPECT_THROW((Jade::Application{config}), std::invalid_argument);
  config.frameLimits = 24;
  config.clearColor.red = std::numeric_limits<float>::quiet_NaN();
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
  auto config = HiddenApplication();
  Jade::Application app(config);
  auto entity = app.world().create();
  auto& pose = entity.add<Jade::Transform>();
  const auto* address = &pose;
  for (int index = 0; index < 64; ++index) {
    app.world().create().add<Jade::Transform>();
  }
  EXPECT_EQ(&entity.get<Jade::Transform>(), address);

  LogCapture capture;
  const auto start = std::chrono::steady_clock::now();
  ASSERT_NO_THROW(app.Run({35, true}));
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  EXPECT_GE(elapsed, (34.0 / 24.0) * 0.85);
  const auto frames = ParseFrames(capture.Text());
  ASSERT_EQ(frames.size(), 35u);
  EXPECT_EQ(frames.front().frame, 1u);
  EXPECT_DOUBLE_EQ(frames.front().fps, 0.0);
  EXPECT_GE(frames.back().second, 2u);
  for (std::size_t index = 0; index < frames.size(); ++index) {
    EXPECT_EQ(frames[index].frame, index + 1);
    EXPECT_TRUE(std::isfinite(frames[index].fps));
    EXPECT_GE(frames[index].fps, 0.0);
    if (index > 0) {
      EXPECT_GE(frames[index].elapsed, frames[index - 1].elapsed);
      const auto expected = frames[index].second == frames[index - 1].second
        ? frames[index - 1].frameInSecond + 1 : 1;
      EXPECT_EQ(frames[index].frameInSecond, expected);
    }
  }

  capture.Reset();
  pose.position = {2, 2, 0};
  ASSERT_NO_THROW(app.Run({2, true}));
  const auto restarted = ParseFrames(capture.Text());
  ASSERT_EQ(restarted.size(), 2u);
  EXPECT_EQ(restarted.front().frame, 1u);
  EXPECT_EQ(restarted.front().second, 1u);
  EXPECT_EQ(restarted.front().frameInSecond, 1u);
  EXPECT_DOUBLE_EQ(restarted.front().fps, 0.0);
  EXPECT_EQ(pose.position, (Jade::Vec3{2, 2, 0}));
}
