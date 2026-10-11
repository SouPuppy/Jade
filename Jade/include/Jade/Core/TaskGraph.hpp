#pragma once

#include <Jade/Core/JobSystem.hpp>
#include <vector>

namespace Jade {
class TaskGraph final {
public:
  class TaskId {
  public:
    TaskId() = default;
  private:
    friend class TaskGraph;
    std::shared_ptr<const int> m_Owner;
    std::size_t m_Index = 0;
  };
  TaskGraph();
  TaskGraph(const TaskGraph&) = delete;
  TaskGraph& operator=(const TaskGraph&) = delete;
  TaskGraph(TaskGraph&&) noexcept = default;
  TaskGraph& operator=(TaskGraph&&) noexcept = default;
  TaskId add(JobSystem::Function function, JobOptions options = {});
  TaskId addParallelFor(std::size_t count, JobSystem::IndexFunction function, JobOptions options = {});
  // Count is evaluated once, after prerequisites complete. Zero completes the node.
  TaskId addParallelFor(std::function<std::size_t()> count, JobSystem::IndexFunction function,
                        JobOptions options = {});
  void depends(TaskId task, TaskId prerequisite);
private:
  friend class JobSystem;
  struct Node {
    JobSystem::Function function;
    std::function<std::size_t()> count;
    JobSystem::IndexFunction parallel;
    JobOptions options;
    std::vector<std::size_t> prerequisites;
  };
  TaskId append(Node node);
  std::shared_ptr<const int> m_Identity;
  std::vector<Node> m_Nodes;
};
} // namespace Jade
