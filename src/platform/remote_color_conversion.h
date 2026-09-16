#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>
#ifdef PARTICLE_VIS_HAVE_LIBYUV
#include <libyuv.h>
#endif

namespace RemoteColorConversion {
inline unsigned char ClampByte(int value)
{
  return static_cast<unsigned char>(std::clamp(value, 0, 255));
}

inline void ScalarRgbaToI420(int width,
                int height,
                const std::vector<unsigned char>& rgba,
                std::vector<unsigned char>& i420)
{
  const std::size_t lumaSize = static_cast<std::size_t>(width) * height;
  i420.resize(lumaSize + lumaSize / 2);
  unsigned char* yPlane = i420.data();
  unsigned char* uPlane = yPlane + lumaSize;
  unsigned char* vPlane = uPlane + lumaSize / 4;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
      const int r = rgba[index];
      const int g = rgba[index + 1];
      const int b = rgba[index + 2];
      yPlane[static_cast<std::size_t>(y) * width + x] =
        ClampByte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
    }
  }

  for (int y = 0; y < height; y += 2) {
    for (int x = 0; x < width; x += 2) {
      int sumU = 0;
      int sumV = 0;
      for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
          const std::size_t index =
            (static_cast<std::size_t>(y + dy) * width + x + dx) * 4;
          const int r = rgba[index];
          const int g = rgba[index + 1];
          const int b = rgba[index + 2];
          sumU += ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
          sumV += ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
        }
      }
      const std::size_t chromaIndex =
        static_cast<std::size_t>(y / 2) * (width / 2) + x / 2;
      uPlane[chromaIndex] = ClampByte((sumU + 2) / 4);
      vPlane[chromaIndex] = ClampByte((sumV + 2) / 4);
    }
  }
}

inline void ScalarI420ToRgba(const unsigned char* yPlane,
                const unsigned char* uPlane,
                const unsigned char* vPlane,
                int width,
                int height,
                int yStride,
                int uvStride,
                std::vector<unsigned char>& rgba)
{
  rgba.resize(static_cast<std::size_t>(width) * height * 4);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int yy = std::max(0, static_cast<int>(yPlane[y * yStride + x]) - 16);
      const int u = static_cast<int>(uPlane[(y / 2) * uvStride + x / 2]) - 128;
      const int v = static_cast<int>(vPlane[(y / 2) * uvStride + x / 2]) - 128;
      const int c = 298 * yy;
      const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
      rgba[index] = ClampByte((c + 409 * v + 128) >> 8);
      rgba[index + 1] = ClampByte((c - 100 * u - 208 * v + 128) >> 8);
      rgba[index + 2] = ClampByte((c + 516 * u + 128) >> 8);
      rgba[index + 3] = 255;
    }
  }
}

inline void RgbaToI420(int width, int height,
                      const std::vector<unsigned char>& rgba,
                      std::vector<unsigned char>& i420)
{
#ifdef PARTICLE_VIS_HAVE_LIBYUV
  const std::size_t size = static_cast<std::size_t>(width) * height;
  i420.resize(size + size / 2);
  // libyuv names register byte order: ABGR is RGBA in little-endian memory.
  if (libyuv::ABGRToI420(rgba.data(), width * 4, i420.data(), width,
      i420.data() + size, width / 2, i420.data() + size + size / 4,
      width / 2, width, height) == 0) return;
#endif
  ScalarRgbaToI420(width, height, rgba, i420);
}

inline void I420ToRgba(const unsigned char* y, const unsigned char* u,
                      const unsigned char* v, int width, int height,
                      int yStride, int uvStride,
                      std::vector<unsigned char>& rgba)
{
#ifdef PARTICLE_VIS_HAVE_LIBYUV
  rgba.resize(static_cast<std::size_t>(width) * height * 4);
  if (libyuv::I420ToABGR(y, yStride, u, uvStride, v, uvStride,
                       rgba.data(), width * 4, width, height) == 0) return;
#endif
  ScalarI420ToRgba(y, u, v, width, height, yStride, uvStride, rgba);
}
} // namespace RemoteColorConversion
