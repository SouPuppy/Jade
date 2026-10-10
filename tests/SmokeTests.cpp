#include <Jade/Application.hpp>
#include <Jade/Color.hpp>
#include <Jade/Geometry/Line.hpp>
#include <Jade/Vector.hpp>

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

bool HasWhiteNear(Jade::Vec2D point, Jade::FramebufferSize size, float viewHeight = 10.0f)
{
  const float pixelsPerUnit = static_cast<float>(size.height) / viewHeight;
  const int x = std::clamp(static_cast<int>(point.x * pixelsPerUnit), 1, size.width - 2);
  const int y = std::clamp(static_cast<int>(point.y * pixelsPerUnit), 1, size.height - 2);
  std::array<unsigned char, 3 * 3 * 4> pixels{};
  glReadBuffer(GL_BACK);
  glReadPixels(x - 1, y - 1, 3, 3, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  for (std::size_t offset = 0; offset < pixels.size(); offset += 4) {
    if (pixels[offset] > 240 && pixels[offset + 1] > 240 && pixels[offset + 2] > 240) {
      return true;
    }
  }
  return false;
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

TEST(Line, OwnsEndpointsAndRejectsInvalidPositions)
{
  Jade::Vec2D start(1.0f, 1.0f);
  Jade::Line line(start, {4.0f, 5.0f});
  start.x = 100.0f;
  EXPECT_FLOAT_EQ(line.Start().x, 1.0f);
  EXPECT_FLOAT_EQ(line.End().x, 4.0f);
  EXPECT_FLOAT_EQ(line.End().y, 5.0f);
  line.setPos({1, 2}, {6, 2});

  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float infinity = std::numeric_limits<float>::infinity();
  EXPECT_THROW(line.setPos({nan, 1}, {2, 3}), std::invalid_argument);
  EXPECT_THROW(line.setPos({9, 9}, {2, infinity}), std::invalid_argument);
  EXPECT_THROW((Jade::Line{{1, 1}, {2, infinity}}), std::invalid_argument);
  EXPECT_FLOAT_EQ(line.Start().x, 1.0f);
  EXPECT_FLOAT_EQ(line.Start().y, 2.0f);
  EXPECT_FLOAT_EQ(line.End().x, 6.0f);
  EXPECT_FLOAT_EQ(line.End().y, 2.0f);
}

TEST(Application, RejectsInvalidConfigurationBeforeCreatingAWindow)
{
  auto config = HiddenApplication();
  config.frameLimits = 0;
  EXPECT_THROW((Jade::Application{config}), std::invalid_argument);
  config.frameLimits = 24;
  for (const float height : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
      std::numeric_limits<float>::quiet_NaN()}) {
    config.viewHeight = height;
    EXPECT_THROW((Jade::Application{config}), std::invalid_argument);
  }
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

TEST(Renderer, DrawsSegmentsAndTracksWindowSize)
{
  Jade::WindowConfig config;
  config.width = 320;
  config.height = 240;
  config.visible = false;
  Jade::Window window(config);
  Jade::Renderer renderer(window, false);
  const Jade::Line diagonal({1, 1}, {4, 5});
  const Jade::Line horizontal({1, 8}, {6, 8});

  // Landscape and portrait windows must retain identical horizontal/vertical units.
  for (const Jade::FramebufferSize requested : {Jade::FramebufferSize{320, 240},
      Jade::FramebufferSize{480, 240}, Jade::FramebufferSize{240, 320}}) {
    auto* nativeWindow = glfwGetCurrentContext();
    ASSERT_NE(nativeWindow, nullptr);
    glfwSetWindowSize(nativeWindow, requested.width, requested.height);
    window.PollEvents();
    const auto size = window.GetFramebufferSize();
    ASSERT_GT(size.width, 2);
    ASSERT_GT(size.height, 2);
    ASSERT_TRUE(renderer.BeginFrame(size, {}));
    ASSERT_TRUE(renderer.ValidateClear({}));
    renderer.DrawLine(diagonal);
    renderer.DrawLine(horizontal);
    EXPECT_TRUE(HasWhiteNear({2.5f, 3.0f}, size));
    EXPECT_FALSE(HasWhiteNear({5.0f, 3.5f}, size));
    EXPECT_TRUE(HasWhiteNear({3.0f, 8.0f}, size));
    EXPECT_FALSE(HasWhiteNear({0.5f, 9.0f}, size));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderer.Present();
  }

  // Reusing the same renderer must upload moved endpoints on the next draw.
  auto changed = diagonal;
  changed.setPos({1, 2}, {6, 2});
  const auto size = window.GetFramebufferSize();
  ASSERT_TRUE(renderer.BeginFrame(size, {}));
  renderer.DrawLine(changed);
  EXPECT_TRUE(HasWhiteNear({3, 2}, size));
  EXPECT_FALSE(HasWhiteNear({2.5f, 3.0f}, size));
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST(Renderer, SkipsDegenerateLinesAndZeroSizedFrames)
{
  Jade::WindowConfig config;
  config.visible = false;
  config.width = 160;
  config.height = 120;
  Jade::Window window(config);
  Jade::Renderer renderer(window, false);
  ASSERT_TRUE(renderer.BeginFrame(window.GetFramebufferSize(), {}));
  renderer.DrawLine(Jade::Line{});
  renderer.DrawLine(Jade::Line{{1, 1}, {1, 1}});
  EXPECT_TRUE(renderer.ValidateClear({}));
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  EXPECT_FALSE(renderer.BeginFrame({0, 0}, {}));
  renderer.DrawLine(Jade::Line{{1, 1}, {4, 5}});
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  EXPECT_THROW((Jade::Renderer{window, false, 0.0f}), std::invalid_argument);
}

TEST(Application, CreatesStableLinesAndLogsPacedFramesAcrossRuns)
{
  auto config = HiddenApplication();
  Jade::Application app(config);
  auto& line = app.create<Jade::Line>();
  line.setPos({1, 1}, {4, 5});
  const auto* firstAddress = &line;
  for (int index = 0; index < 64; ++index) {
    app.create<Jade::Line>();
  }
  auto& segment = app.create<Jade::Line>(Jade::Vec2D{1, 8}, Jade::Vec2D{6, 8});
  EXPECT_EQ(&line, firstAddress);
  EXPECT_FLOAT_EQ(line.Start().x, 1.0f);
  EXPECT_FLOAT_EQ(line.End().x, 4.0f);
  EXPECT_FLOAT_EQ(segment.Start().y, 8.0f);
  EXPECT_FLOAT_EQ(segment.End().x, 6.0f);

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
  line.setPos({2, 2}, {5, 4});
  ASSERT_NO_THROW(app.Run({2, true}));
  const auto restarted = ParseFrames(capture.Text());
  ASSERT_EQ(restarted.size(), 2u);
  EXPECT_EQ(restarted.front().frame, 1u);
  EXPECT_EQ(restarted.front().second, 1u);
  EXPECT_EQ(restarted.front().frameInSecond, 1u);
  EXPECT_DOUBLE_EQ(restarted.front().fps, 0.0);
  EXPECT_FLOAT_EQ(line.Start().x, 2.0f);
  EXPECT_FLOAT_EQ(line.End().x, 5.0f);
}
