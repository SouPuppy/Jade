#include "internal/Core/FrameDiagnostics.hpp"

#include <gtest/gtest.h>

using namespace std::chrono_literals;
using Diagnostics = Jade::Detail::FrameDiagnostics;

TEST(FrameDiagnostics, SummarizesOneSecondAndSeparatesPacingFromWork)
{
  const Diagnostics::TimePoint start{};
  Diagnostics diagnostics(25);
  const auto first = diagnostics.FinishFrame(start - 5ms, start);
  EXPECT_DOUBLE_EQ(first.intervalMs, 0.0);
  EXPECT_DOUBLE_EQ(first.workMs, 5.0);
  EXPECT_FALSE(first.summary);
  for (int index = 1; index <= 25; ++index) {
    const auto completion = start + index * 40ms;
    const auto sample = diagnostics.FinishFrame(completion - (index == 25 ? 10ms : 2ms), completion);
    EXPECT_FALSE(sample.hitch);
    if (index < 25) {
      EXPECT_FALSE(sample.summary);
    } else {
      ASSERT_TRUE(sample.summary);
      EXPECT_DOUBLE_EQ(sample.summary->fps, 25.0);
      EXPECT_DOUBLE_EQ(sample.summary->intervalAvgMs, 40.0);
      EXPECT_DOUBLE_EQ(sample.summary->intervalMaxMs, 40.0);
      EXPECT_DOUBLE_EQ(sample.summary->workAvgMs, 2.32);
      EXPECT_DOUBLE_EQ(sample.summary->workMaxMs, 10.0);
    }
  }
  for (int index = 1; index <= 25; ++index) {
    const auto completion = start + 1s + index * 40ms;
    const auto sample = diagnostics.FinishFrame(completion - 1ms, completion);
    if (index < 25) EXPECT_FALSE(sample.summary);
    else {
      ASSERT_TRUE(sample.summary);
      EXPECT_DOUBLE_EQ(sample.summary->workAvgMs, 1.0);
      EXPECT_DOUBLE_EQ(sample.summary->workMaxMs, 1.0);
    }
  }
}

TEST(FrameDiagnostics, IgnoresStartupAndJitterAndRateLimitsHitches)
{
  const Diagnostics::TimePoint start{};
  Diagnostics diagnostics(24); // Hitch threshold is 125 ms, not the 41.67 ms cap.
  EXPECT_FALSE(diagnostics.FinishFrame(start - 300ms, start).hitch);
  EXPECT_FALSE(diagnostics.FinishFrame(start + 42ms, start + 44ms).hitch);
  EXPECT_TRUE(diagnostics.FinishFrame(start + 198ms, start + 200ms).hitch);
  EXPECT_FALSE(diagnostics.FinishFrame(start + 498ms, start + 500ms).hitch);
  EXPECT_TRUE(diagnostics.FinishFrame(start + 1198ms, start + 1200ms).hitch);
  EXPECT_THROW(Diagnostics(0), std::invalid_argument);
}

TEST(FrameDiagnostics, DoesNotCountMinimizedTimeAndRestartsTheSummaryWindow)
{
  const Diagnostics::TimePoint start{};
  Diagnostics diagnostics(24);
  diagnostics.FinishFrame(start, start);
  diagnostics.FinishFrame(start + 38ms, start + 40ms);
  diagnostics.Resume();
  const auto restored = diagnostics.FinishFrame(start + 10s - 2ms, start + 10s);
  EXPECT_FALSE(restored.summary);
  EXPECT_FALSE(restored.hitch);
  EXPECT_DOUBLE_EQ(restored.intervalMs, 0.0);
  const auto next = diagnostics.FinishFrame(start + 10s + 38ms, start + 10s + 40ms);
  EXPECT_FALSE(next.summary);
  EXPECT_FALSE(next.hitch);
  EXPECT_DOUBLE_EQ(next.intervalMs, 40.0);
}

TEST(FrameDiagnostics, HandlesCoincidentCompletionTimesWithoutZeroDivision)
{
  const Diagnostics::TimePoint start{};
  Diagnostics diagnostics(24);
  diagnostics.FinishFrame(start, start);
  const auto duplicate = diagnostics.FinishFrame(start, start);
  EXPECT_FALSE(duplicate.summary);
  const auto next = diagnostics.FinishFrame(start + 998ms, start + 1s);
  ASSERT_TRUE(next.summary);
  EXPECT_DOUBLE_EQ(next.summary->fps, 1.0);
  EXPECT_DOUBLE_EQ(next.summary->intervalAvgMs, 1000.0);
  EXPECT_DOUBLE_EQ(next.summary->workAvgMs, 2.0);
}
