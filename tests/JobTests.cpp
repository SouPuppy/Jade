#include <Jade/Core/TaskGraph.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <latch>
#include <semaphore>
#include <thread>
#include <vector>

using namespace Jade;

TEST(Jobs, ParallelRangesEmptyWorkAndConcurrentProducers) {
  JobSystem jobs({3});
  std::vector<std::atomic<unsigned>> hits(10003);
  jobs.wait(jobs.parallelFor(hits.size(), [&](auto i) { ++hits[i]; }, {JobCategory::Physics, {}, 7}));
  for (const auto& hit : hits) EXPECT_EQ(hit.load(), 1u);
  jobs.wait(jobs.parallelFor(0, [](auto) { FAIL() << "Empty range executed"; }));
  TaskGraph empty; jobs.wait(jobs.submit(empty));
  std::atomic<unsigned> count = 0;
  std::vector<std::jthread> producers;
  for (int p = 0; p < 4; ++p) producers.emplace_back([&] {
    std::vector<JobHandle> handles;
    for (int i = 0; i < 250; ++i) handles.push_back(jobs.submit([&] { ++count; }, JobCategory::Asset));
    for (const auto& handle : handles) jobs.wait(handle);
  });
  producers.clear();
  EXPECT_EQ(count.load(), 1000u);
  jobs.shutdown();
  for (const auto& category : jobs.statistics().categories) {
    EXPECT_EQ(category.queued, 0u); EXPECT_EQ(category.running, 0u);
  }
}

TEST(Jobs, GraphForkJoinDynamicCountsReuseAndPriorityInheritance) {
  JobSystem jobs({1});
  std::latch entered(1); std::binary_semaphore release(0);
  auto gate = jobs.submit([&] { entered.count_down(); release.acquire(); }, JobCategory::Physics);
  entered.wait();
  std::vector<int> events;
  auto gameplay = jobs.submit([&] { events.push_back(3); }, JobCategory::Gameplay);
  TaskGraph graph;
  std::size_t size = 0;
  std::vector<int> values;
  auto prepare = graph.add([&] { events.push_back(1); size = 41; values.assign(size, 0); }, {JobCategory::Asset});
  auto first = graph.addParallelFor([&] { return size; }, [&](auto i) { values[i] += 1; }, {JobCategory::Physics});
  auto second = graph.addParallelFor([&] { return size; }, [&](auto i) { values[i] *= 3; }, {JobCategory::Physics});
  auto join = graph.add([&] { events.push_back(2); }, {JobCategory::Physics});
  graph.depends(first, prepare); graph.depends(second, first); graph.depends(join, second);
  auto result = jobs.submit(graph);
  release.release(); jobs.wait(gate); jobs.wait(result); jobs.wait(gameplay);
  ASSERT_EQ(events.size(), 3u); EXPECT_EQ(events[0], 1);
  for (auto value : values) EXPECT_EQ(value, 3);
  jobs.wait(jobs.submit(graph));
  for (auto value : values) EXPECT_EQ(value, 3);
  // Independent branches write disjoint storage and join after both finish.
  TaskGraph fork;
  std::array<int, 2> branches{};
  auto a = fork.add([&] { branches[0] = 2; });
  auto b = fork.add([&] { branches[1] = 4; });
  auto c = fork.add([&] { EXPECT_EQ(branches[0] + branches[1], 6); });
  fork.depends(c, a); fork.depends(c, b); jobs.wait(jobs.submit(fork));
}

TEST(Jobs, FastRootsCannotResubmitTheirSuccessorsDuringSubmission) {
  JobSystem jobs({6});
  for (unsigned round = 0; round < 24; ++round) {
    std::vector<std::atomic<unsigned>> executions(512);
    TaskGraph graph; auto previous = graph.add([&] { ++executions[0]; });
    for (std::size_t i = 1; i < executions.size(); ++i) {
      const auto next = graph.add([&,i] { ++executions[i]; }); graph.depends(next,previous); previous = next;
    }
    jobs.wait(jobs.submit(graph));
    for (const auto& count : executions) EXPECT_EQ(count.load(),1u);
  }
}

TEST(Jobs, WeightedPrioritiesAndBackgroundCategoriesMakeProgress) {
  JobSystem jobs({1});
  std::latch entered(1); std::binary_semaphore release(0);
  auto gate = jobs.submit([&] { entered.count_down(); release.acquire(); }, JobCategory::Physics);
  entered.wait();
  std::vector<JobCategory> order;
  std::vector<JobHandle> handles;
  for (const auto category : {JobCategory::Physics, JobCategory::Gameplay, JobCategory::Rendering,
                              JobCategory::RayTracing, JobCategory::Asset})
    for (int i = 0; i < 100; ++i) handles.push_back(jobs.submit([&, category] { order.push_back(category); }, category));
  release.release(); jobs.wait(gate);
  for (const auto& handle : handles) jobs.wait(handle);
  ASSERT_EQ(order.size(), 500u);
  std::array<unsigned, 6> counts{};
  for (std::size_t i = 0; i < 60; ++i) ++counts[static_cast<std::size_t>(order[i])];
  EXPECT_GT(counts[0], counts[1]); EXPECT_GT(counts[1], counts[2]);
  EXPECT_GT(counts[2], counts[3] + counts[4]);
  EXPECT_GT(counts[3], 0u); EXPECT_GT(counts[4], 0u);
}

TEST(Jobs, WorkIsStolenFromBlockedWorkerLocalQueues) {
  JobSystem jobs({2}); std::latch completed(12);
  auto parent = jobs.submit([&] {
    TaskGraph graph;
    for (int i = 0; i < 12; ++i) graph.add([&] { completed.count_down(); });
    const auto children = jobs.submit(graph);
    completed.wait(); // Force the other worker to steal these local tasks.
    jobs.wait(children);
  });
  jobs.wait(parent);
  EXPECT_GE(jobs.statistics().categories[static_cast<std::size_t>(JobCategory::General)].stolen, 12u);
}

TEST(Jobs, NestedWaitsUseOneWorkerAndExternalHelpersStayWithinTheirGroup) {
  JobSystem jobs({1}); std::atomic<int> count = 0;
  std::function<void(int)> recurse = [&](int depth) {
    ++count;
    if (depth) jobs.wait(jobs.submit([&, depth] { recurse(depth - 1); }));
  };
  jobs.wait(jobs.submit([&] { recurse(5); })); EXPECT_EQ(count.load(), 6);
  std::latch entered(1); std::binary_semaphore release(0);
  auto gate = jobs.submit([&] { entered.count_down(); release.acquire(); }); entered.wait();
  bool unrelated = false, target = false;
  auto background = jobs.submit([&] { unrelated = true; });
  auto physics = jobs.submit([&] { target = true; }, JobCategory::Physics);
  jobs.wait(physics, WaitMode::Help);
  EXPECT_TRUE(target); EXPECT_FALSE(unrelated);
  release.release(); jobs.wait(gate); jobs.wait(background);
}

TEST(Jobs, ExceptionsDrainRunningWorkSkipSuccessorsAndDoNotPoisonOtherGroups) {
  JobSystem jobs({2}); std::latch entered(1), throwing(1); std::binary_semaphore release(0);
  std::atomic<bool> successor = false;
  TaskGraph graph;
  graph.add([&] { entered.count_down(); release.acquire(); });
  auto fail = graph.add([&] { entered.wait(); throwing.count_down(); throw std::runtime_error("job failure"); });
  auto next = graph.add([&] { successor = true; }); graph.depends(next, fail);
  auto handle = jobs.submit(graph); throwing.wait(); EXPECT_FALSE(handle.ready());
  release.release(); EXPECT_THROW(jobs.wait(handle), std::runtime_error);
  EXPECT_FALSE(successor.load());
  int independent = 0; jobs.wait(jobs.submit([&] { independent = 42; })); EXPECT_EQ(independent, 42);
  EXPECT_THROW(jobs.wait(jobs.parallelFor(200, [](auto i) { if (i == 0) throw std::runtime_error("range"); },
                                        {JobCategory::Physics, {}, 1})), std::runtime_error);
  jobs.shutdown();
  for (const auto& category : jobs.statistics().categories) { EXPECT_EQ(category.queued, 0u); EXPECT_EQ(category.running, 0u); }
}

TEST(Jobs, RejectsCyclesForeignIdsInvalidConfigAndAncestorWaits) {
  EXPECT_THROW(JobSystem(JobSystemConfig{257}), std::invalid_argument);
  JobSystemConfig invalid; invalid.priorityWeights[2] = 0;
  EXPECT_THROW(JobSystem jobs(invalid), std::invalid_argument);
  JobSystem jobs({1}), other({1});
  EXPECT_THROW(jobs.wait(JobHandle{}), std::invalid_argument);
  EXPECT_THROW(jobs.wait(other.submit([] {})), std::invalid_argument);
  EXPECT_THROW(jobs.submit([] {}, JobCategory::Count), std::invalid_argument);
  EXPECT_THROW(jobs.submit({}, JobCategory::General), std::invalid_argument);
  TaskGraph graph, foreign;
  auto a = graph.add([] {}), b = graph.add([] {});
  EXPECT_THROW(graph.depends(a, foreign.add([] {})), std::invalid_argument);
  graph.depends(a, b); graph.depends(b, a);
  EXPECT_THROW(jobs.submit(graph), std::invalid_argument);
  JobHandle parent; std::latch started(1); std::binary_semaphore published(0);
  parent = jobs.submit([&] {
    started.count_down(); published.acquire();
    EXPECT_THROW(jobs.wait(parent), std::logic_error);
    jobs.wait(jobs.submit([&] { EXPECT_THROW(jobs.wait(parent), std::logic_error); }));
    EXPECT_THROW(jobs.shutdown(), std::logic_error);
  });
  started.wait(); published.release(); jobs.wait(parent);
  jobs.shutdown(); jobs.shutdown(); EXPECT_THROW(jobs.submit([] {}), std::logic_error);
}

TEST(Jobs, ShutdownDrainsAcceptedWorkAndNestedSubmissions) {
  JobSystem jobs({2}); std::latch entered(1); std::binary_semaphore release(0);
  std::atomic<int> result = 0;
  auto parent = jobs.submit([&] {
    entered.count_down(); release.acquire();
    jobs.wait(jobs.submit([&] { ++result; }));
  });
  entered.wait();
  std::jthread closer([&] { jobs.shutdown(); });
  bool rejected = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!rejected && std::chrono::steady_clock::now() < deadline) {
    try { jobs.submit([] {}); } catch (const std::logic_error&) { rejected = true; }
    std::this_thread::yield();
  }
  release.release(); closer.join();
  EXPECT_TRUE(rejected); EXPECT_EQ(result.load(), 1); EXPECT_TRUE(parent.ready()); jobs.wait(parent);
}
