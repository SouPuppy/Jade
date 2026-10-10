#pragma once

#include <Jade/Renderer/Backend.hpp>

#include <condition_variable>
#include <mutex>
#include <thread>

namespace Jade::Detail {

class RenderSession final {
public:
  ~RenderSession();
  void install(std::unique_ptr<Renderer::Backend> backend);
  bool configured() const noexcept { return static_cast<bool>(m_Backend); }
  void start();
  void stop() noexcept;
  void invalidate() noexcept;
  void submit(Renderer::SceneSnapshot snapshot, Renderer::ImageSize framebuffer);
  std::shared_ptr<const Renderer::Image> image() const;
  Renderer::RenderProgress progress() const;
  void rethrowError();

private:
  struct Input {
    Renderer::SceneSnapshot snapshot;
    Renderer::ImageSize size;
    std::uint64_t epoch = 0, revision = 0;
  };
  void run(std::stop_token stop);
  std::unique_ptr<Renderer::Backend> m_Backend;
  Renderer::RenderSettings m_Settings;
  Renderer::SceneCompiler m_Compiler;
  mutable std::mutex m_Mutex;
  std::condition_variable m_Ready;
  std::shared_ptr<const Input> m_Pending, m_LastInput;
  std::shared_ptr<const Renderer::Image> m_Image;
  Renderer::RenderProgress m_Progress;
  std::exception_ptr m_Error;
  std::uint64_t m_Epoch = 0, m_Revision = 0;
  std::stop_source m_Cancel;
  std::jthread m_Thread;
};

} // namespace Jade::Detail
