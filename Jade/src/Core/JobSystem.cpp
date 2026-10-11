#include <Jade/Core/JobSystem.hpp>
#include <Jade/Core/TaskGraph.hpp>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Jade {
namespace Detail {
struct JobNode;
struct ReadyQueue;
struct ReadyWork {
  JobState* group = nullptr;
  JobNode* node = nullptr;
  ReadyWork *previous = nullptr, *next = nullptr;
  ReadyQueue* queue = nullptr;
  std::size_t begin = 0, end = 0;
  bool chunk = false;
};
constexpr std::size_t NoWorker = std::numeric_limits<std::size_t>::max();
struct ReadyQueue {
  ReadyWork *first = nullptr, *last = nullptr;
  std::size_t owner = NoWorker;
};
struct JobNode {
  JobSystem::Function function;
  std::function<std::size_t()> count;
  JobSystem::IndexFunction parallel;
  JobOptions options;
  std::size_t priority = 0, prerequisites = 0;
  std::vector<std::size_t> successors;
  ReadyWork starter;
  std::unique_ptr<ReadyWork[]> chunks;
  std::size_t slots = 0, active = 0, next = 0, countValue = 0, grain = 0;
};
struct JobState {
  std::shared_ptr<const int> owner;
  std::weak_ptr<JobState> parent;
  std::vector<std::unique_ptr<JobNode>> nodes;
  std::atomic<bool> complete = false;
  std::exception_ptr error;
  std::size_t remaining = 0, running = 0;
};
} // namespace Detail
namespace {
using namespace Detail;
thread_local const void* currentPool = nullptr;
thread_local std::size_t currentWorker = NoWorker;
thread_local std::shared_ptr<JobState> currentGroup;
std::size_t category(const JobNode& n) { return static_cast<std::size_t>(n.options.category); }
std::size_t queueIndex(const JobNode& n) { return n.priority * 6 + category(n); }
std::size_t defaultPriority(JobCategory value) {
  switch (value) {
  case JobCategory::Physics: return 0;
  case JobCategory::Gameplay: return 1;
  case JobCategory::Rendering: return 2;
  default: return 3;
  }
}
bool belongsTo(JobState* state, const std::shared_ptr<JobState>& target) {
  if (!target) return true;
  if (state == target.get()) return true;
  auto ancestor = state->parent.lock();
  while (ancestor) {
    if (ancestor == target) return true;
    ancestor = ancestor->parent.lock();
  }
  return false;
}
struct ExecutionScope {
  const void* pool = currentPool;
  std::size_t worker = currentWorker;
  std::shared_ptr<JobState> group = currentGroup;
  ExecutionScope(const void* p, std::size_t w, std::shared_ptr<JobState> g) {
    currentPool = p; currentWorker = w; currentGroup = std::move(g);
  }
  ~ExecutionScope() { currentPool = pool; currentWorker = worker; currentGroup = std::move(group); }
};
} // namespace

struct JobSystem::Impl {
  JobSystemConfig config;
  std::shared_ptr<const int> identity = std::make_shared<const int>(0);
  std::mutex mutex, shutdownMutex;
  std::condition_variable changed;
  std::array<ReadyQueue, 24> injected;
  std::vector<std::array<ReadyQueue, 24>> local;
  std::vector<std::jthread> workers;
  std::unordered_map<JobState*, std::shared_ptr<JobState>> active;
  std::array<std::array<std::size_t, 6>, 4> readyCounts{};
  JobStatistics stats;
  std::size_t priorityCursor = 0;
  std::array<std::size_t, 4> categoryCursor{};
  std::uint32_t budget = 0;
  bool closing = false;

  explicit Impl(JobSystemConfig c) : config(c) {
    if (config.workerCount > 256 || std::any_of(config.priorityWeights.begin(), config.priorityWeights.end(),
                                               [](auto weight) { return weight == 0; }))
      throw std::invalid_argument("Workers must be in [0,256] and priority weights must be positive.");
    const auto hardware = std::thread::hardware_concurrency();
    config.workerCount = config.workerCount ? config.workerCount : std::clamp(hardware > 1 ? hardware - 1 : 1u, 1u, 256u);
    stats.workerCount = config.workerCount;
    budget = config.priorityWeights[0];
    local.resize(config.workerCount);
    for (std::size_t i = 0; i < local.size(); ++i) for (auto& queue : local[i]) queue.owner = i;
    try {
      for (std::size_t i = 0; i < local.size(); ++i) workers.emplace_back([this, i] { run(i); });
    } catch (...) {
      close();
      throw;
    }
  }
  void push(ReadyWork& work, std::size_t worker) noexcept {
    auto& node = *work.node;
    auto& queue = worker == NoWorker ? injected[queueIndex(node)] : local[worker][queueIndex(node)];
    work.queue = &queue; work.previous = queue.last; work.next = nullptr;
    if (queue.last) queue.last->next = &work; else queue.first = &work;
    queue.last = &work;
    ++readyCounts[node.priority][category(node)];
    ++stats.categories[category(node)].queued;
  }
  void remove(ReadyWork& work) noexcept {
    auto& queue = *work.queue;
    if (work.previous) work.previous->next = work.next; else queue.first = work.next;
    if (work.next) work.next->previous = work.previous; else queue.last = work.previous;
    work.queue = nullptr; work.previous = work.next = nullptr;
    --readyCounts[work.node->priority][category(*work.node)];
    --stats.categories[category(*work.node)].queued;
  }
  static ReadyWork* find(ReadyQueue& queue, bool back, const std::shared_ptr<JobState>& target) {
    auto* work = back ? queue.last : queue.first;
    while (work && !belongsTo(work->group, target)) work = back ? work->previous : work->next;
    return work;
  }
  ReadyWork* find(std::size_t p, std::size_t c, std::size_t worker, const std::shared_ptr<JobState>& target) {
    if (!readyCounts[p][c]) return nullptr;
    const auto index = p * 6 + c;
    if (worker != NoWorker) if (auto* work = find(local[worker][index], true, target)) return work;
    if (auto* work = find(injected[index], false, target)) return work;
    for (std::size_t offset = 1; offset <= local.size(); ++offset) {
      const auto victim = ((worker == NoWorker ? 0 : worker) + offset) % local.size();
      if (victim == worker) continue;
      if (auto* work = find(local[victim][index], false, target)) return work;
    }
    return nullptr;
  }
  std::pair<ReadyWork*, std::shared_ptr<JobState>> take(std::size_t worker,
                                                     const std::shared_ptr<JobState>& target = {}) {
    const auto claim = [&](ReadyWork* work) {
      const auto c = category(*work->node);
      auto group = active.at(work->group);
      if (work->queue->owner != NoWorker && worker != NoWorker && work->queue->owner != worker)
        ++stats.categories[c].stolen;
      remove(*work);
      ++group->running; ++stats.categories[c].running;
      return std::pair{work, std::move(group)};
    };
    // External helpers must not reset/consume workers' fairness budgets while
    // skipping unrelated queues. Their extra execution stays scoped to the wait.
    if (target) {
      for (std::size_t p = 0; p < 4; ++p) for (std::size_t c = 0; c < 6; ++c)
        if (auto* work = find(p, c, worker, target)) return claim(work);
      return {};
    }
    for (std::size_t attempt = 0; attempt < 4; ++attempt) {
      const auto p = priorityCursor;
      for (std::size_t offset = 0; offset < 6; ++offset) {
        const auto c = (categoryCursor[p] + offset) % 6;
        if (auto* work = find(p, c, worker, target)) {
          categoryCursor[p] = (c + 1) % 6;
          if (--budget == 0) advancePriority();
          return claim(work);
        }
      }
      advancePriority();
    }
    return {};
  }
  void advancePriority() noexcept {
    priorityCursor = (priorityCursor + 1) % 4;
    budget = config.priorityWeights[priorityCursor];
  }
  void complete(const std::shared_ptr<JobState>& group) noexcept {
    if (group->running == 0 && (group->error || group->remaining == 0)) {
      group->complete.store(true, std::memory_order_release);
      active.erase(group.get());
    }
  }
  void fail(const std::shared_ptr<JobState>& group, std::exception_ptr error) noexcept {
    if (group->error) return;
    group->error = std::move(error);
    for (const auto& node : group->nodes) {
      const auto discard = [&](ReadyWork& work) {
        if (work.queue) { remove(work); ++stats.categories[category(*node)].cancelled; }
      };
      discard(node->starter);
      for (std::size_t i = 0; i < node->slots; ++i) discard(node->chunks[i]);
    }
  }
  void finishNode(JobNode& node, const std::shared_ptr<JobState>& group, std::size_t worker) noexcept {
    --group->remaining;
    for (const auto successor : node.successors) {
      auto& next = *group->nodes[successor];
      if (--next.prerequisites == 0) push(next.starter, worker);
    }
  }
  void assignChunk(ReadyWork& work, JobNode& node, std::size_t worker) noexcept {
    work.begin = node.next;
    work.end = node.next + std::min(node.grain, node.countValue - node.next);
    node.next = work.end;
    push(work, worker);
  }
  void execute(ReadyWork& work, const std::shared_ptr<JobState>& group, std::size_t worker) {
    ExecutionScope scope(this, worker, group);
    auto& node = *work.node;
    std::exception_ptr error;
    std::unique_ptr<ReadyWork[]> prepared;
    std::size_t count = 0, grain = 0, slots = 0;
    try {
      if (work.chunk) {
        for (auto i = work.begin; i < work.end; ++i) node.parallel(i);
      } else if (node.count) {
        count = node.count();
        const auto width = std::size_t{config.workerCount} * 4;
        grain = node.options.grainSize ? node.options.grainSize : std::max(std::size_t{1}, count / width + (count % width != 0));
        const auto chunks = count / grain + (count % grain != 0);
        slots = std::min(chunks, width);
        if (slots) prepared = std::make_unique<ReadyWork[]>(slots);
      } else node.function();
    } catch (...) { error = std::current_exception(); }
    {
      std::lock_guard lock(mutex);
      auto& categoryStats = stats.categories[category(node)];
      --group->running; --categoryStats.running;
      if (error) { ++categoryStats.failed; fail(group, std::move(error)); }
      else ++categoryStats.completed;
      if (!group->error) {
        if (work.chunk) {
          if (node.next < node.countValue) assignChunk(work, node, worker);
          else if (--node.active == 0) finishNode(node, group, worker);
        } else if (node.count && slots) {
          node.countValue = count; node.grain = grain; node.slots = slots; node.active = slots;
          node.chunks = std::move(prepared);
          for (std::size_t i = 0; i < slots; ++i) {
            auto& chunk = node.chunks[i]; chunk.group = group.get(); chunk.node = &node; chunk.chunk = true;
            assignChunk(chunk, node, worker);
          }
        } else finishNode(node, group, worker);
      }
      complete(group);
    }
    changed.notify_all();
  }
  void run(std::size_t worker) {
    for (;;) {
      std::unique_lock lock(mutex);
      auto task = take(worker);
      if (!task.first) {
        if (closing && active.empty()) return;
        changed.wait(lock);
        continue;
      }
      lock.unlock();
      execute(*task.first, task.second, worker);
    }
  }
  void close() {
    std::lock_guard shutdownLock(shutdownMutex);
    { std::lock_guard lock(mutex); closing = true; }
    changed.notify_all();
    for (auto& worker : workers) if (worker.joinable()) worker.join();
  }
};

bool JobHandle::ready() const noexcept { return m_State && m_State->complete.load(std::memory_order_acquire); }
JobSystem::JobSystem(JobSystemConfig config) : m_Impl(std::make_unique<Impl>(config)) {}
JobSystem::~JobSystem() { m_Impl->close(); }
std::uint32_t JobSystem::workerCount() const noexcept { return m_Impl->config.workerCount; }
JobStatistics JobSystem::statistics() const { std::lock_guard lock(m_Impl->mutex); return m_Impl->stats; }
void JobSystem::shutdown() {
  if (currentPool == m_Impl.get()) throw std::logic_error("Cannot shut down a job system from its jobs.");
  m_Impl->close();
}
JobHandle JobSystem::submit(Function function, JobCategory categoryValue, std::optional<JobPriority> priority) {
  return submit(std::move(function), JobOptions{categoryValue, priority, 0});
}
JobHandle JobSystem::submit(Function function, JobOptions options) {
  TaskGraph graph; graph.add(std::move(function), options); return submit(graph);
}
JobHandle JobSystem::parallelFor(std::size_t count, IndexFunction function, JobOptions options) {
  TaskGraph graph; graph.addParallelFor(count, std::move(function), options); return submit(graph);
}
JobHandle JobSystem::submit(const TaskGraph& graph) {
  auto group = std::make_shared<JobState>();
  group->owner = m_Impl->identity;
  if (currentPool == m_Impl.get()) group->parent = currentGroup;
  group->remaining = graph.m_Nodes.size();
  group->nodes.reserve(group->remaining);
  for (const auto& source : graph.m_Nodes) {
    const auto c = static_cast<std::size_t>(source.options.category);
    const auto p = source.options.priority ? static_cast<std::size_t>(*source.options.priority) : defaultPriority(source.options.category);
    if (c >= 6 || p >= 4) throw std::invalid_argument("Invalid job category or priority.");
    auto node = std::make_unique<JobNode>();
    node->function = source.function; node->count = source.count; node->parallel = source.parallel;
    node->options = source.options; node->priority = p; node->prerequisites = source.prerequisites.size();
    node->starter.group = group.get(); node->starter.node = node.get();
    group->nodes.push_back(std::move(node));
  }
  std::vector<std::size_t> remaining, order;
  remaining.reserve(group->nodes.size()); order.reserve(group->nodes.size());
  for (std::size_t i = 0; i < group->nodes.size(); ++i) {
    remaining.push_back(group->nodes[i]->prerequisites);
    for (const auto predecessor : graph.m_Nodes[i].prerequisites) group->nodes[predecessor]->successors.push_back(i);
    if (remaining.back() == 0) order.push_back(i);
  }
  for (std::size_t i = 0; i < order.size(); ++i)
    for (const auto successor : group->nodes[order[i]]->successors) if (--remaining[successor] == 0) order.push_back(successor);
  if (order.size() != group->nodes.size()) throw std::invalid_argument("Task graph contains a dependency cycle.");
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    auto& node = *group->nodes[*it];
    for (const auto successor : node.successors) node.priority = std::min(node.priority, group->nodes[successor]->priority);
  }
  {
    std::lock_guard lock(m_Impl->mutex);
    if (m_Impl->closing && !(currentPool == m_Impl.get() && currentGroup))
      throw std::logic_error("Job system is shut down.");
    if (group->nodes.empty()) group->complete.store(true, std::memory_order_release);
    else {
      m_Impl->active.emplace(group.get(), group);
      const auto worker = currentPool == m_Impl.get() ? currentWorker : NoWorker;
      for (auto& node : group->nodes) if (!node->prerequisites) m_Impl->push(node->starter, worker);
    }
  }
  m_Impl->changed.notify_all();
  return JobHandle(std::move(group));
}
void JobSystem::wait(const JobHandle& handle, WaitMode mode) {
  if (!handle.m_State || handle.m_State->owner != m_Impl->identity)
    throw std::invalid_argument("Wait requires a handle from this job system.");
  if (currentPool == m_Impl.get()) {
    auto ancestor = currentGroup;
    while (ancestor) {
      if (ancestor == handle.m_State) throw std::logic_error("A job cannot wait for itself or an ancestor.");
      ancestor = ancestor->parent.lock();
    }
  }
  const auto worker = currentPool == m_Impl.get() ? currentWorker : NoWorker;
  const bool cooperate = (currentPool == m_Impl.get() && currentGroup) || mode == WaitMode::Help;
  std::unique_lock lock(m_Impl->mutex);
  while (!handle.ready()) {
    if (cooperate) {
      auto task = m_Impl->take(worker, worker == NoWorker ? handle.m_State : std::shared_ptr<JobState>{});
      if (task.first) { lock.unlock(); m_Impl->execute(*task.first, task.second, worker); lock.lock(); continue; }
    }
    m_Impl->changed.wait(lock);
  }
  const auto error = handle.m_State->error;
  lock.unlock();
  if (error) std::rethrow_exception(error);
}
} // namespace Jade
