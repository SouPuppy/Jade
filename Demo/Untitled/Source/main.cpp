#include "Game.hpp"
#include <windows.h>
#include <charconv>
#include <cmath>
#include <iostream>
#include <string_view>

namespace {
std::filesystem::path executableDirectory() {
  std::wstring path(32768, L'\0');
  const auto count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (!count || count == path.size()) throw std::runtime_error("Cannot locate Untitled executable.");
  path.resize(count);
  return std::filesystem::path(path).parent_path();
}
}
int main(int argc, char* argv[]) {
  try {
    Jade::EngineConfig config;
    config.window.title = "Untitled | WASD Move   Q/E Rotate   R Reset   Esc Exit";
    config.frameLimits = 24;
    config.debug.shortcuts = true;
    config.renderer.pipeline = Jade::Renderer::RenderMode::Raster;
    config.renderer.rayTracing.device = Jade::Renderer::RayDevice::GPU;
    config.renderer.rayTracing.resolutionScale = 0.5f;
    config.renderer.rayTracing.minSamplesPerFrame = 1;
    config.renderer.rayTracing.maxBounces = 4;
    std::uint32_t frames = 0;
    bool hidden = false;
    bool benchmark = false;
    bool soul = false;
    for (int i = 1; i < argc; ++i) {
      const std::string_view option(argv[i]);
      if (option == "--help") {
        std::cout << "Untitled [--pipeline raster|hybrid|tracing|none] [--soul] [--device cpu|gpu] [--scale 0.1..1] [--frames N] [--hidden] [--benchmark]\n"
          "WASD: camera-relative movement | Q/E: rotate 45 degrees | R: reset | Esc: exit\n"
          "Engine debug: I Raster/Hybrid/RayTracing/None | F1 wireframe | F2 soul camera | F3 scene bounds | F4 collisions | F5 color/normals | O projection | P pause\n";
        return 0;
      }
      if (option == "--soul") { soul = true; continue; }
      if (option == "--hidden") { hidden = true; continue; }
      if (option == "--benchmark") { benchmark = true; continue; }
      if (i + 1 == argc) throw std::invalid_argument("Missing option value.");
      const std::string_view value(argv[++i]);
      if (option == "--pipeline") {
        using Jade::Renderer::RenderMode;
        if(value=="raster") config.renderer.pipeline=RenderMode::Raster;
        else if(value=="hybrid") config.renderer.pipeline=RenderMode::Hybrid;
        else if(value=="tracing") config.renderer.pipeline=RenderMode::RayTracing;
        else if(value=="none") config.renderer.pipeline=RenderMode::None;
        else throw std::invalid_argument("Pipeline must be raster, hybrid, tracing or none.");
      } else if (option == "--device") {
        if (value != "cpu" && value != "gpu") throw std::invalid_argument("Device must be cpu or gpu.");
        config.renderer.rayTracing.device = value == "cpu" ? Jade::Renderer::RayDevice::CPU : Jade::Renderer::RayDevice::GPU;
      } else if (option == "--scale") {
        float scale = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), scale);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !std::isfinite(scale) || scale < 0.1f || scale > 1)
          throw std::invalid_argument("Scale must be within [0.1,1].");
        config.renderer.rayTracing.resolutionScale = scale;
      } else if (option == "--frames") {
        const auto result = std::from_chars(value.data(), value.data() + value.size(), frames);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !frames)
          throw std::invalid_argument("Frames must be a positive integer.");
      } else throw std::invalid_argument("Unknown option: " + std::string(option));
    }
    config.window.visible = !hidden;
    std::unique_ptr<Jade::Application> app;
    try { app = std::make_unique<Jade::Application>(config); }
    catch (const std::exception& error) {
      if (config.renderer.rayTracing.device != Jade::Renderer::RayDevice::GPU) throw;
      const Jade::Log::Logger logger{"Untitled"};
      logger << Jade::Log::warn("GPU initialization failed; using CPU: {}", error.what());
      config.renderer.rayTracing.device = Jade::Renderer::RayDevice::CPU;
      app = std::make_unique<Jade::Application>(config);
    }
    Untitled::Game game(app->runtime(), executableDirectory() / "UntitledAssets");
    app->runtime().setScene(game.scene());
    if(soul) app->runtime().setSoulMode(true);
    double elapsed = 0;
    int cameraStep = 0;
    if (benchmark) {
      if (!frames) frames = 1441;
      app->on<Jade::FrameUpdate>([&](const Jade::FrameUpdate& frame) {
        elapsed += frame.dt;
        const std::array<Jade::Vec2,8> inputs = {{{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}}};
        game.queueInput(inputs[static_cast<std::size_t>(elapsed / 2) % 8]);
        const int nextStep = static_cast<int>(elapsed / 4) % 8;
        if (nextStep != cameraStep) { game.rotate(nextStep - cameraStep); cameraStep = nextStep; }
      });
    }
    app->Run({frames});
    app->runtime().setScene(nullptr);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Untitled: " << error.what() << '\n';
    return 1;
  }
}
