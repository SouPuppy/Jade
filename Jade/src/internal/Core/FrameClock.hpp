#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <stdexcept>

namespace Jade::Detail {

struct FrameStats {
  double elapsed = 0.0;
  std::uint64_t second = 1;
  std::uint64_t frame = 0;
  std::uint64_t frameInSecond = 0;
  double fps = 0.0;
};

// Absolute deadlines avoid accumulated drift. Supply completion times so
// scheduling and statistics can be tested without sleeping or a GPU.
class FrameClock final {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  FrameClock(std::uint32_t frameLimits, TimePoint start)
    : m_Start(start), m_Deadline(start)
  {
    if (frameLimits == 0) {
      throw std::invalid_argument("Frame limits must be positive.");
    }
    m_Period = std::chrono::duration_cast<Clock::duration>(
      std::chrono::duration<double>(1.0 / frameLimits));
    if (m_Period <= Clock::duration::zero()) {
      m_Period = Clock::duration(1);
    }
  }

  TimePoint NextDeadline() const noexcept { return m_Deadline; }
  void Resume(TimePoint now) noexcept { m_Deadline = now; }

  FrameStats FinishFrame(TimePoint now)
  {
    FrameStats stats;
    stats.elapsed = std::chrono::duration<double>(now - m_Start).count();
    stats.second = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(now - m_Start).count()) + 1;
    stats.frame = ++m_Frames;
    if (stats.second != m_Second) {
      m_Second = stats.second;
      m_FramesInSecond = 0;
    }
    stats.frameInSecond = ++m_FramesInSecond;

    m_Completions.push_back(now);
    while (m_Completions.size() > 1 && now - m_Completions.front() > std::chrono::seconds(1)) {
      m_Completions.pop_front();
    }
    const double sampleDuration = std::chrono::duration<double>(now - m_Completions.front()).count();
    if (sampleDuration > 0.0) {
      stats.fps = static_cast<double>(m_Completions.size() - 1) / sampleDuration;
    }

    m_Deadline += m_Period;
    if (m_Deadline < now) {
      // Skip missed slots instead of issuing a burst of catch-up frames.
      m_Deadline += m_Period * ((now - m_Deadline) / m_Period + 1);
    }
    return stats;
  }

private:
  TimePoint m_Start;
  TimePoint m_Deadline;
  Clock::duration m_Period{};
  std::uint64_t m_Frames = 0;
  std::uint64_t m_Second = 1;
  std::uint64_t m_FramesInSecond = 0;
  std::deque<TimePoint> m_Completions;
};

} // namespace Jade::Detail
