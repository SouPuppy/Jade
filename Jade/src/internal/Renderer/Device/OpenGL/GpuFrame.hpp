#pragma once
#include <Jade/Renderer/Common/Image.hpp>
#include <glad/gl.h>
#include <atomic>

namespace Jade::Detail {
// Slots are owned/deleted by the compute backend. Shared handles pin a slot;
// consumer fences protect reuse after the display releases its CPU handle.
struct GpuFrame {
  GLuint texture = 0;
  GLsync ready = nullptr;
  std::atomic<GLsync> consumed = nullptr;
  Renderer::ImageSize size;
};
} // namespace Jade::Detail
