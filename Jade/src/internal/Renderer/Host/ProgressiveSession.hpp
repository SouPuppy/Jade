#pragma once

#include <Jade/Renderer/RayTracing/ProgressiveBackend.hpp>
#include <Jade/Core/JobSystem.hpp>

#include <condition_variable>
#include <chrono>
#include <mutex>
#include <thread>

namespace Jade::Detail {

class ProgressiveSession final {
public:
  explicit ProgressiveSession(JobSystem* jobs = nullptr) : m_Jobs(jobs) {}
  ~ProgressiveSession();
  void install(std::unique_ptr<Renderer::ProgressiveBackend> backend);
  bool configured() const noexcept { return static_cast<bool>(m_Backend); }
  void bind(Renderer::ProgressiveBackend &backend);
  void updateSettings(const Renderer::ProgressiveSettings&);
  void start();
  void stop() noexcept;
  void invalidate() noexcept;
  // Running frames have independent accumulation and a display deadline.
  // Paused/headless submissions keep accumulating an unchanged snapshot.
  void submit(Renderer::SceneSnapshot snapshot, Renderer::ImageSize framebuffer,
              bool accumulate = true,
              std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());
  // A running display frame may only present after its full-image round(s) finish.
  void waitForFrame();
  std::shared_ptr<const Renderer::Image> image() const;
  std::shared_ptr<GpuFrame> gpuFrame() const;
  Renderer::RenderProgress progress() const;
  void rethrowError();

private:
  JobSystem* m_Jobs;
  struct Input {
    Renderer::SceneSnapshot snapshot;
    Renderer::ImageSize size;
    std::uint64_t epoch = 0, revision = 0;
    bool accumulate = true;
    std::chrono::steady_clock::time_point deadline;
  };
  void run(std::stop_token stop);
  std::unique_ptr<Renderer::ProgressiveBackend> m_Owned;
  Renderer::ProgressiveBackend *m_Backend = nullptr;
  bool m_Gpu = false;
  Renderer::ProgressiveSettings m_Settings;
  Renderer::ImageEncoding m_Encoding = Renderer::ImageEncoding::Radiance;
  std::uint64_t m_SettingsRevision = 0;
  bool m_ResolvePending = false;
  Renderer::RaySceneCompiler m_Compiler;
  mutable std::mutex m_Mutex;
  std::condition_variable m_Ready;
  std::condition_variable m_Finished;
  std::shared_ptr<const Input> m_Pending, m_LastInput;
  std::shared_ptr<const Renderer::Image> m_Image;
  std::shared_ptr<GpuFrame> m_GpuFrame;
  Renderer::RenderProgress m_Progress;
  std::exception_ptr m_Error;
  std::uint64_t m_Epoch = 0, m_Revision = 0;
  std::uint64_t m_CompletedRevision = 0;
  bool m_Stopped = true;
  std::stop_source m_Cancel;
  std::jthread m_Thread;
};

} // namespace Jade::Detail
