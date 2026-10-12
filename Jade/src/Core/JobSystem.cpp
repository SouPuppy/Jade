#include <Jade/Core/JobSystem.hpp>
#include <Jade/Core/TaskGraph.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Jade {
namespace Detail {
struct JobNode {
  JobSystem::Function function;
  std::function<std::size_t()> count;
  JobSystem::IndexFunction parallel;
  JobOptions options;
  std::size_t priority = 0, countValue = 0, grain = 0;
  std::atomic<std::size_t> prerequisites{0}, cursor{0}, leases{0};
  std::vector<std::size_t> successors;
};
struct JobState {
  std::shared_ptr<const int> owner;
  std::weak_ptr<JobState> parent;
  std::vector<std::unique_ptr<JobNode>> nodes;
  std::mutex mutex;
  std::atomic<bool> complete{false}, failed{false};
  std::exception_ptr error;
  std::size_t remaining = 0, running = 0;
};
}
namespace {
using namespace Detail;
using Clock = std::chrono::steady_clock;
constexpr auto NoWorker = std::numeric_limits<std::size_t>::max();
std::uint64_t nanos(Clock::duration d) { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count()); }
thread_local const void* currentPool = nullptr;
thread_local std::size_t currentWorker = NoWorker;
thread_local std::shared_ptr<JobState> currentGroup;
thread_local std::uint64_t waitNanos = 0;
std::size_t category(const JobNode& n) { return static_cast<std::size_t>(n.options.category); }
std::size_t priority(JobCategory c) { return c == JobCategory::Physics ? 0 : c == JobCategory::Gameplay ? 1 : c == JobCategory::Rendering ? 2 : 3; }
bool belongsTo(const std::shared_ptr<JobState>& group,const std::shared_ptr<JobState>& target) {
  if (!target || group == target) return true;
  auto parent = group->parent.lock();
  while (parent) { if (parent == target) return true; parent = parent->parent.lock(); } return false;
}
struct ExecutionScope {
  const void* pool = currentPool; std::size_t worker = currentWorker; std::shared_ptr<JobState> group = currentGroup;
  ExecutionScope(const void* p,std::size_t w,std::shared_ptr<JobState> g) { currentPool = p; currentWorker = w; currentGroup = std::move(g); }
  ~ExecutionScope() { currentPool = pool; currentWorker = worker; currentGroup = std::move(group); }
};
struct Ready {
  std::shared_ptr<JobState> group; JobNode* node = nullptr; bool chunk = false;
  Clock::time_point queued = Clock::now();
};
struct QueueSet { std::mutex mutex; std::array<std::deque<Ready>,24> queues; };
struct CategoryCounters {
  std::atomic<std::uint64_t> queued{0},running{0},completed{0},failed{0},cancelled{0},stolen{0};
};
struct alignas(64) WorkerCounters {
  std::atomic<std::uint64_t> tasks{0},stolen{0},busy{0},idle{0},queueWait{0},lockWait{0};
  std::array<std::atomic<std::uint64_t>,6> categoryBusy{};
  std::size_t priorityCursor = 0; std::array<std::size_t,4> categoryCursor{}; std::uint32_t budget = 0;
};
JobWorkerStatistics snapshot(const WorkerCounters& c) {
  JobWorkerStatistics s; s.tasks = c.tasks.load(); s.stolen = c.stolen.load(); s.busyNanoseconds = c.busy.load(); s.idleNanoseconds = c.idle.load();
  s.queueWaitNanoseconds = c.queueWait.load(); s.lockWaitNanoseconds = c.lockWait.load();
  for (unsigned i = 0; i < 6; ++i) s.categoryBusyNanoseconds[i] = c.categoryBusy[i].load(); return s;
}
}
struct JobSystem::Impl {
  JobSystemConfig config;
  std::shared_ptr<const int> identity = std::make_shared<const int>(0);
  // Control state is not consulted for normal task claims or range leases.
  std::mutex control, shutdownMutex, sleepMutex;
  std::condition_variable changed;
  QueueSet injected;
  std::vector<std::unique_ptr<QueueSet>> local;
  std::vector<std::unique_ptr<WorkerCounters>> counters;
  WorkerCounters helper;
  std::array<CategoryCounters,6> categories;
  std::array<std::atomic<std::size_t>,24> readyCounts{};
  std::vector<std::jthread> workers;
  std::unordered_map<JobState*,std::shared_ptr<JobState>> active;
  std::atomic<std::uint64_t> epoch{0};
  std::atomic<std::size_t> activeCount{0},pending{0};
  std::atomic<std::uint32_t> runningWorkers{0},peak{0};
  std::atomic<bool> closing{false};
  explicit Impl(JobSystemConfig c) : config(c) {
    if (c.workerCount > 256 || std::any_of(c.priorityWeights.begin(),c.priorityWeights.end(),[](auto w) { return !w; }))
      throw std::invalid_argument("Invalid job worker count or priority weights.");
    const auto hardware = std::thread::hardware_concurrency();
    config.workerCount = c.workerCount ? c.workerCount : std::clamp(hardware > 1 ? hardware-1 : 1u,1u,256u);
    helper.budget = config.priorityWeights[0];
    for (unsigned i = 0; i < config.workerCount; ++i) {
      local.push_back(std::make_unique<QueueSet>()); counters.push_back(std::make_unique<WorkerCounters>()); counters.back()->budget = config.priorityWeights[0];
    }
    try { for (unsigned i = 0; i < config.workerCount; ++i) workers.emplace_back([this,i] { run(i); }); }
    catch (...) { close(); throw; }
  }
  void signal(bool all = false) {
    { std::lock_guard lock(sleepMutex); epoch.fetch_add(1,std::memory_order_release); }
    if (all) changed.notify_all(); else changed.notify_one();
  }
  void push(Ready task,std::size_t worker) {
    const auto index = task.node->priority*6+category(*task.node);
    auto& q = worker == NoWorker ? injected : *local[worker];
    { std::lock_guard lock(q.mutex); q.queues[index].push_back(std::move(task)); ++readyCounts[index]; ++pending; ++categories[index%6].queued; }
    signal();
  }
  std::optional<Ready> pop(QueueSet& q,std::size_t index,bool back,const std::shared_ptr<JobState>& target,WorkerCounters& profile) {
    const auto start = Clock::now(); std::unique_lock lock(q.mutex);
    profile.lockWait.fetch_add(nanos(Clock::now()-start),std::memory_order_relaxed);
    auto& queue = q.queues[index]; if (queue.empty()) return {};
    auto it = back ? std::prev(queue.end()) : queue.begin();
    while (!belongsTo(it->group,target)) {
      if (back) { if (it == queue.begin()) return {}; --it; }
      else { if (++it == queue.end()) return {}; }
    }
    Ready task = std::move(*it); queue.erase(it); --readyCounts[index]; --pending; --categories[index%6].queued; return task;
  }
  std::optional<Ready> find(std::size_t index,std::size_t worker,const std::shared_ptr<JobState>& target) {
    if (!readyCounts[index].load(std::memory_order_relaxed)) return {};
    auto& profile = worker == NoWorker ? helper : *counters[worker];
    if (worker != NoWorker) if (auto task = pop(*local[worker],index,true,target,profile)) return task;
    if (auto task = pop(injected,index,false,target,profile)) return task;
    for (std::size_t offset = 1; offset <= local.size(); ++offset) {
      const auto victim = ((worker == NoWorker ? 0 : worker)+offset)%local.size(); if (victim == worker) continue;
      if (auto task = pop(*local[victim],index,false,target,profile)) {
        if (worker != NoWorker) { ++profile.stolen; ++categories[index%6].stolen; } return task;
      }
    }
    return {};
  }
  std::optional<Ready> take(std::size_t worker,const std::shared_ptr<JobState>& target = {}) {
    auto& profile = worker == NoWorker ? helper : *counters[worker];
    for (;;) {
      std::optional<Ready> task;
      if (target) {
        for (std::size_t i = 0; i < 24 && !task; ++i) task = find(i,worker,target);
      } else {
        for (unsigned attempt = 0; attempt < 4 && !task; ++attempt) {
          const auto p = profile.priorityCursor;
          for (unsigned offset = 0; offset < 6 && !task; ++offset) {
            const auto c = (profile.categoryCursor[p]+offset)%6; task = find(p*6+c,worker,{});
            if (task) profile.categoryCursor[p] = (c+1)%6;
          }
          if (!task || --profile.budget == 0) {
            profile.priorityCursor = (p+1)%4; profile.budget = config.priorityWeights[profile.priorityCursor];
          }
        }
      }
      if (!task) return {};
      {
        std::lock_guard lock(task->group->mutex);
        if (!task->group->failed.load(std::memory_order_acquire)) {
          ++task->group->running; ++categories[category(*task->node)].running;
          profile.queueWait.fetch_add(nanos(Clock::now()-task->queued),std::memory_order_relaxed); return task;
        }
      }
      ++categories[category(*task->node)].cancelled;
    }
  }
  void complete(const std::shared_ptr<JobState>& group) {
    // Called with the group's mutex. No unrelated graph can delay this decision.
    if (!group->running && (group->failed.load() || !group->remaining) && !group->complete.exchange(true,std::memory_order_release)) {
      { std::lock_guard lock(control); active.erase(group.get()); --activeCount; } signal(true);
    }
  }
  void finishNode(JobNode& node,const std::shared_ptr<JobState>& group,std::size_t worker) {
    --group->remaining;
    for (const auto successor : node.successors) {
      auto& next = *group->nodes[successor];
      if (next.prerequisites.fetch_sub(1,std::memory_order_acq_rel) == 1) push({group,&next,false},worker);
    }
  }
  void execute(Ready task,std::size_t worker) {
    const bool outer = currentPool != this || currentWorker != worker;
    ExecutionScope scope(this,worker,task.group); auto& node = *task.node; auto& group = task.group;
    auto& profile = worker == NoWorker ? helper : *counters[worker]; const auto start = Clock::now(); const auto waits = waitNanos;
    if (worker != NoWorker && outer) {
      const auto count = runningWorkers.fetch_add(1)+1; auto value = peak.load(); while (value < count && !peak.compare_exchange_weak(value,count)) {}
    }
    bool finished = false, requeue = false; std::exception_ptr error;
    try {
      if (task.chunk) {
        // Several atomic leases per queue visit amortize locking while retaining fairness.
        for (unsigned lease = 0; lease < 8 && !group->failed.load(std::memory_order_acquire); ++lease) {
          const auto first = node.cursor.fetch_add(node.grain,std::memory_order_relaxed); if (first >= node.countValue) break;
          const auto end = first+std::min(node.grain,node.countValue-first);
          for (auto i = first; i < end; ++i) node.parallel(i);
        }
        requeue = node.cursor.load(std::memory_order_relaxed) < node.countValue;
        if (!requeue) finished = node.leases.fetch_sub(1,std::memory_order_acq_rel) == 1;
      } else if (node.count) {
        node.countValue = node.count();
        const auto width = std::size_t{config.workerCount}*32;
        node.grain = node.options.grainSize ? node.options.grainSize : std::max(std::size_t{1},node.countValue/width+(node.countValue%width != 0));
        const auto chunks = node.countValue/node.grain+(node.countValue%node.grain != 0);
        const auto leases = std::min(chunks,std::size_t{config.workerCount}*2); node.leases = leases;
        for (std::size_t i = 0; i < leases; ++i) push({group,&node,true},worker);
        finished = !leases;
      } else { node.function(); finished = true; }
    } catch (...) { error = std::current_exception(); }
    const auto duration = nanos(Clock::now()-start), waited = waitNanos-waits;
    const auto busy = duration > waited ? duration-waited : 0;
    ++profile.tasks; profile.busy.fetch_add(busy,std::memory_order_relaxed); profile.categoryBusy[category(node)].fetch_add(busy,std::memory_order_relaxed);
    if (worker != NoWorker && outer) --runningWorkers;
    {
      std::lock_guard lock(group->mutex); auto& stats = categories[category(node)];
      --group->running; --stats.running;
      if (error) { ++stats.failed; if (!group->failed.load()) { group->error = error; group->failed.store(true,std::memory_order_release); } }
      else ++stats.completed;
      if (!group->failed.load()) {
        if (requeue) push({group,&node,true},worker);
        else if (finished) finishNode(node,group,worker);
      }
      complete(group);
    }
    signal(true);
  }
  void run(std::size_t worker) {
    for (;;) {
      const auto observed = epoch.load(std::memory_order_acquire);
      if (auto task = take(worker)) { execute(std::move(*task),worker); continue; }
      if (closing.load() && !activeCount.load() && !pending.load()) return;
      const auto start = Clock::now(); std::unique_lock lock(sleepMutex);
      changed.wait(lock,[&] { return epoch.load(std::memory_order_acquire) != observed || (closing.load() && !activeCount.load() && !pending.load()); });
      counters[worker]->idle.fetch_add(nanos(Clock::now()-start),std::memory_order_relaxed);
    }
  }
  void close() {
    std::lock_guard lock(shutdownMutex); { std::lock_guard stateLock(control); closing = true; } signal(true);
    for (auto& worker : workers) if (worker.joinable()) worker.join();
  }
};
bool JobHandle::ready() const noexcept { return m_State && m_State->complete.load(std::memory_order_acquire); }
JobSystem::JobSystem(JobSystemConfig c) : m_Impl(std::make_unique<Impl>(c)) {}
JobSystem::~JobSystem() { m_Impl->close(); }
std::uint32_t JobSystem::workerCount() const noexcept { return m_Impl->config.workerCount; }
JobStatistics JobSystem::statistics() const {
  JobStatistics s; s.workerCount = workerCount(); s.peakRunningWorkers = m_Impl->peak.load(); s.helper = snapshot(m_Impl->helper);
  for (const auto& worker : m_Impl->counters) s.workers.push_back(snapshot(*worker));
  for (unsigned i = 0; i < 6; ++i) {
    const auto& c = m_Impl->categories[i]; s.categories[i] = {c.queued.load(),c.running.load(),c.completed.load(),c.failed.load(),c.cancelled.load(),c.stolen.load()};
  }
  return s;
}
void JobSystem::shutdown() { if (currentPool == m_Impl.get()) throw std::logic_error("Cannot shut down a job system from its jobs."); m_Impl->close(); }
JobHandle JobSystem::submit(Function f,JobCategory c,std::optional<JobPriority> p) { return submit(std::move(f),{c,p,0}); }
JobHandle JobSystem::submit(Function f,JobOptions o) { TaskGraph g; g.add(std::move(f),o); return submit(g); }
JobHandle JobSystem::parallelFor(std::size_t n,IndexFunction f,JobOptions o) { TaskGraph g; g.addParallelFor(n,std::move(f),o); return submit(g); }
JobHandle JobSystem::submit(const TaskGraph& graph) {
  auto group = std::make_shared<JobState>(); group->owner = m_Impl->identity;
  if (currentPool == m_Impl.get()) group->parent = currentGroup; group->remaining = graph.m_Nodes.size(); group->nodes.reserve(group->remaining);
  for (const auto& source : graph.m_Nodes) {
    const auto c = static_cast<std::size_t>(source.options.category);
    const auto p = source.options.priority ? static_cast<std::size_t>(*source.options.priority) : priority(source.options.category);
    if (c >= 6 || p >= 4) throw std::invalid_argument("Invalid job category or priority.");
    auto node = std::make_unique<JobNode>(); node->function = source.function; node->count = source.count; node->parallel = source.parallel;
    node->options = source.options; node->priority = p; node->prerequisites = source.prerequisites.size(); group->nodes.push_back(std::move(node));
  }
  std::vector<std::size_t> remaining,order;
  for (std::size_t i = 0; i < group->nodes.size(); ++i) {
    remaining.push_back(group->nodes[i]->prerequisites.load());
    for (const auto predecessor : graph.m_Nodes[i].prerequisites) group->nodes[predecessor]->successors.push_back(i);
    if (!remaining.back()) order.push_back(i);
  }
  for (std::size_t i = 0; i < order.size(); ++i) for (const auto successor : group->nodes[order[i]]->successors) if (--remaining[successor] == 0) order.push_back(successor);
  if (order.size() != group->nodes.size()) throw std::invalid_argument("Task graph contains a dependency cycle.");
  for (auto it = order.rbegin(); it != order.rend(); ++it) for (const auto successor : group->nodes[*it]->successors)
    group->nodes[*it]->priority = std::min(group->nodes[*it]->priority,group->nodes[successor]->priority);
  {
    std::lock_guard lock(m_Impl->control);
    if (m_Impl->closing.load() && !(currentPool == m_Impl.get() && currentGroup)) throw std::logic_error("Job system is shut down.");
    if (group->nodes.empty()) group->complete.store(true,std::memory_order_release);
    else { m_Impl->active.emplace(group.get(),group); ++m_Impl->activeCount; }
  }
  const auto worker = currentPool == m_Impl.get() ? currentWorker : NoWorker;
  // Workers may finish an earlier root before this loop reaches its successors.
  // Root selection must use the immutable graph, not the live dependency count.
  try { for (std::size_t i = 0; i < group->nodes.size(); ++i) if (graph.m_Nodes[i].prerequisites.empty()) m_Impl->push({group,group->nodes[i].get(),false},worker); }
  catch (...) {
    std::lock_guard lock(group->mutex); group->error = std::current_exception(); group->failed = true; m_Impl->complete(group); throw;
  }
  m_Impl->signal(true); return JobHandle(std::move(group));
}
void JobSystem::wait(const JobHandle& handle,WaitMode mode) {
  if (!handle.m_State || handle.m_State->owner != m_Impl->identity) throw std::invalid_argument("Wait requires a handle from this job system.");
  if (currentPool == m_Impl.get()) {
    auto parent = currentGroup; while (parent) { if (parent == handle.m_State) throw std::logic_error("Cannot wait for self or an ancestor."); parent = parent->parent.lock(); }
  }
  const auto start = Clock::now(); const auto worker = currentPool == m_Impl.get() ? currentWorker : NoWorker;
  const bool cooperate = (currentPool == m_Impl.get() && currentGroup) || mode == WaitMode::Help;
  while (!handle.ready()) {
    const auto observed = m_Impl->epoch.load(std::memory_order_acquire);
    if (cooperate) if (auto task = m_Impl->take(worker,worker == NoWorker ? handle.m_State : std::shared_ptr<JobState>{})) { m_Impl->execute(std::move(*task),worker); continue; }
    std::unique_lock lock(m_Impl->sleepMutex);
    m_Impl->changed.wait(lock,[&] { return handle.ready() || m_Impl->epoch.load(std::memory_order_acquire) != observed; });
  }
  waitNanos += nanos(Clock::now()-start);
  std::exception_ptr error; { std::lock_guard lock(handle.m_State->mutex); error = handle.m_State->error; }
  if (error) std::rethrow_exception(error);
}
} // namespace Jade
