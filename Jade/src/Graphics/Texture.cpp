#include <Jade/Graphics/Texture.hpp>

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Jade {
namespace {
void check(HRESULT result) {
  if (FAILED(result)) throw std::runtime_error("WIC PNG decoding failed: " + std::to_string(result));
}
float linear(std::uint8_t byte) {
  const float x = byte / 255.0f;
  return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
}
}
Texture::Texture(int width, int height, std::vector<std::uint8_t> rgba)
  : m_Width(width), m_Height(height), m_Pixels(std::move(rgba)) {
  if (width <= 0 || height <= 0 || static_cast<std::uint64_t>(width) * height * 4 != m_Pixels.size())
    throw std::invalid_argument("Texture requires positive dimensions and matching RGBA8 pixels.");
}
Texture Texture::loadPng(const std::filesystem::path& path) {
  const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) check(initialized);
  struct ComGuard { bool owned; ~ComGuard() { if (owned) CoUninitialize(); } } guard{SUCCEEDED(initialized)};
  using Microsoft::WRL::ComPtr;
  ComPtr<IWICImagingFactory> factory;
  check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
  ComPtr<IWICBitmapDecoder> decoder;
  check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder));
  GUID format;
  check(decoder->GetContainerFormat(&format));
  if (format != GUID_ContainerFormatPng) throw std::invalid_argument("Texture loader requires a PNG file.");
  ComPtr<IWICBitmapFrameDecode> frame;
  check(decoder->GetFrame(0, &frame));
  UINT width, height;
  check(frame->GetSize(&width, &height));
  const auto bytes = static_cast<std::uint64_t>(width) * height * 4;
  if (!width || !height || width > INT_MAX || height > INT_MAX || bytes > UINT_MAX)
    throw std::invalid_argument("PNG dimensions exceed the supported range.");
  ComPtr<IWICFormatConverter> converter;
  check(factory->CreateFormatConverter(&converter));
  check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                              nullptr, 0, WICBitmapPaletteTypeCustom));
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(bytes));
  check(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(bytes), pixels.data()));
  return Texture(static_cast<int>(width), static_cast<int>(height), std::move(pixels));
}
Texel Texture::sample(Vec2 uv) const {
  if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) throw std::invalid_argument("Texture UV must be finite.");
  const int x = static_cast<int>(std::min(static_cast<double>(m_Width - 1), static_cast<double>(std::clamp(uv.x, 0.0f, 1.0f)) * m_Width));
  const int y = static_cast<int>(std::min(static_cast<double>(m_Height - 1), static_cast<double>(std::clamp(uv.y, 0.0f, 1.0f)) * m_Height));
  const auto offset = (static_cast<std::size_t>(y) * m_Width + x) * 4;
  return {{linear(m_Pixels[offset]), linear(m_Pixels[offset + 1]), linear(m_Pixels[offset + 2])}, m_Pixels[offset + 3] / 255.0f};
}
} // namespace Jade
