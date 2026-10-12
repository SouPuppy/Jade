#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace Jade {
namespace Detail { struct JobState; }
class TaskGraph;

enum class JobCategory { Physics, Gameplay, Rendering, RayTracing, Asset, General, Count };
enum class JobPriority { Critical, High, Normal, Background, Count };
enum class WaitMode { Block, Help };

struct JobOptions {
  JobCategory category = JobCategory::General;
  std::optional<JobPriority> priority;
  // Zero selects fine-grained leases, claimed in batches without queue locks.
  std::size_t grainSize = 0;
};
struct JobSystemConfig {
  // Zero selects hardware_concurrency - 1, clamped to [1, 256].
  std::uint32_t workerCount = 0;
  std::array<std::uint32_t, 4> priorityWeights{8, 4, 2, 1};
};
struct JobCategoryStatistics {
  // Counts scheduler work items, including parallel-node expansion and chunks.
  std::uint64_t queued = 0, running = 0, completed = 0, failed = 0, cancelled = 0, stolen = 0;
};
struct JobWorkerStatistics {
  std::uint64_t tasks = 0, stolen = 0, busyNanoseconds = 0, idleNanoseconds = 0;
  std::uint64_t queueWaitNanoseconds = 0, lockWaitNanoseconds = 0;
  std::array<std::uint64_t,6> categoryBusyNanoseconds{};
};
struct JobStatistics {
  std::uint32_t workerCount = 0;
  std::array<JobCategoryStatistics, 6> categories{};
  std::vector<JobWorkerStatistics> workers;
  JobWorkerStatistics helper;
  std::uint32_t peakRunningWorkers = 0;
};

class JobHandle final {
public:
  JobHandle() = default;
  bool valid() const noexcept { return static_cast<bool>(m_State); }
  bool ready() const noexcept;
private:
  friend class JobSystem;
  explicit JobHandle(std::shared_ptr<Detail::JobState> state) : m_State(std::move(state)) {}
  std::shared_ptr<Detail::JobState> m_State;
};

// Callables may run concurrently. Captures must outlive completion; the scheduler
// does not infer memory dependencies or make World/Registry access thread-safe.
class JobSystem final {
public:
  using Function = std::function<void()>;
  using IndexFunction = std::function<void(std::size_t)>;
  explicit JobSystem(JobSystemConfig config = {});
  ~JobSystem();
  JobSystem(const JobSystem&) = delete;
  JobSystem& operator=(const JobSystem&) = delete;
  JobHandle submit(Function function, JobCategory category = JobCategory::General,
                   std::optional<JobPriority> priority = {});
  JobHandle submit(Function function, JobOptions options);
  JobHandle parallelFor(std::size_t count, IndexFunction function, JobOptions options = {});
  JobHandle submit(const TaskGraph& graph);
  // Workers cooperatively dispatch while waiting, even in Block mode. External
  // Help callers execute only this handle's graph and descendant submissions.
  void wait(const JobHandle& handle, WaitMode mode = WaitMode::Block);
  std::uint32_t workerCount() const noexcept;
  JobStatistics statistics() const;
  // Stop external submissions, drain accepted work and internal descendants, join.
  // Must be called outside this system's running jobs; callbacks must terminate.
  void shutdown();
private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace Jade
