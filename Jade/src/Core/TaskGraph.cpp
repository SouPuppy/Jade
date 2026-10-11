#include <Jade/Core/TaskGraph.hpp>
#include <algorithm>
#include <stdexcept>

namespace Jade {
TaskGraph::TaskGraph() : m_Identity(std::make_shared<const int>(0)) {}
TaskGraph::TaskId TaskGraph::append(Node node) {
  if (!m_Identity) m_Identity = std::make_shared<const int>(0);
  TaskId id; id.m_Owner = m_Identity; id.m_Index = m_Nodes.size();
  m_Nodes.push_back(std::move(node));
  return id;
}
TaskGraph::TaskId TaskGraph::add(JobSystem::Function function, JobOptions options) {
  if (!function) throw std::invalid_argument("Job function cannot be empty.");
  return append({std::move(function), {}, {}, options, {}});
}
TaskGraph::TaskId TaskGraph::addParallelFor(std::size_t count, JobSystem::IndexFunction function,
                                         JobOptions options) {
  return addParallelFor([count] { return count; }, std::move(function), options);
}
TaskGraph::TaskId TaskGraph::addParallelFor(std::function<std::size_t()> count,
                                         JobSystem::IndexFunction function, JobOptions options) {
  if (!count || !function) throw std::invalid_argument("Parallel job functions cannot be empty.");
  return append({{}, std::move(count), std::move(function), options, {}});
}
void TaskGraph::depends(TaskId task, TaskId prerequisite) {
  if (task.m_Owner != m_Identity || prerequisite.m_Owner != m_Identity ||
      !task.m_Owner || task.m_Index >= m_Nodes.size() || prerequisite.m_Index >= m_Nodes.size())
    throw std::invalid_argument("Task dependencies must belong to this graph.");
  auto& dependencies = m_Nodes[task.m_Index].prerequisites;
  if (std::find(dependencies.begin(), dependencies.end(), prerequisite.m_Index) == dependencies.end())
    dependencies.push_back(prerequisite.m_Index);
}
} // namespace Jade
