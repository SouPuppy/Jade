#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace Jade::Detail {

struct PerformanceSummary {
  double fps = 0.0;
  double intervalAvgMs = 0.0;
  double intervalMaxMs = 0.0;
  double workAvgMs = 0.0;
  double workMaxMs = 0.0;
};

struct FrameSample {
  double intervalMs = 0.0;
  double workMs = 0.0;
  bool hitch = false;
  std::optional<PerformanceSummary> summary;
};

// Allocation-free measurements. Intervals include pacing; work is wall time from
// held/update callbacks through present, including any blocking inside present.
class FrameDiagnostics final {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  explicit FrameDiagnostics(std::uint32_t targetFps)
  {
    if (targetFps == 0) {
      throw std::invalid_argument("Target FPS must be positive.");
    }
    m_HitchThresholdMs = std::max(100.0, 3.0 * 1000.0 / targetFps);
  }

  void Resume() noexcept
  {
    m_Previous.reset();
    ResetWindow();
  }

  FrameSample FinishFrame(TimePoint started, TimePoint completed) noexcept
  {
    FrameSample sample;
    sample.workMs = Milliseconds(completed - started);
    if (!m_Previous) {
      m_Previous = completed;
      m_WindowStart = completed;
      return sample; // Warm up without reporting zero FPS or a startup hitch.
    }
    sample.intervalMs = Milliseconds(completed - *m_Previous);
    m_Previous = completed;
    if (sample.intervalMs <= 0.0) {
      return sample;
    }

    ++m_Intervals;
    m_IntervalTotalMs += sample.intervalMs;
    m_IntervalMaxMs = std::max(m_IntervalMaxMs, sample.intervalMs);
    m_WorkTotalMs += sample.workMs;
    m_WorkMaxMs = std::max(m_WorkMaxMs, sample.workMs);

    if (sample.intervalMs > m_HitchThresholdMs
        && (!m_LastHitch || completed - *m_LastHitch >= std::chrono::seconds(1))) {
      sample.hitch = true;
      m_LastHitch = completed;
    }
    if (completed - m_WindowStart >= std::chrono::seconds(1)) {
      const double count = static_cast<double>(m_Intervals);
      sample.summary = PerformanceSummary{
        count * 1000.0 / m_IntervalTotalMs,
        m_IntervalTotalMs / count, m_IntervalMaxMs,
        m_WorkTotalMs / count, m_WorkMaxMs};
      m_WindowStart = completed;
      ResetWindow();
    }
    return sample;
  }

private:
  static double Milliseconds(Clock::duration duration) noexcept
  {
    return std::chrono::duration<double, std::milli>(duration).count();
  }

  void ResetWindow() noexcept
  {
    m_Intervals = 0;
    m_IntervalTotalMs = m_IntervalMaxMs = 0.0;
    m_WorkTotalMs = m_WorkMaxMs = 0.0;
  }

  double m_HitchThresholdMs = 0.0;
  std::optional<TimePoint> m_Previous;
  std::optional<TimePoint> m_LastHitch;
  TimePoint m_WindowStart{};
  std::uint64_t m_Intervals = 0;
  double m_IntervalTotalMs = 0.0;
  double m_IntervalMaxMs = 0.0;
  double m_WorkTotalMs = 0.0;
  double m_WorkMaxMs = 0.0;
};

} // namespace Jade::Detail
