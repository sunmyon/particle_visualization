#include "image/image_io.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>

namespace {

void AppendEncodedBytes(void* context, void* data, int size)
{
  if (!context || !data || size <= 0) return;
  auto& output = *static_cast<std::vector<unsigned char>*>(context);
  const auto* bytes = static_cast<const unsigned char*>(data);
  output.insert(output.end(), bytes, bytes + size);
}

} // namespace

bool WritePngRgb(const char* filename,
                 int width,
                 int height,
                 const std::vector<unsigned char>& rgb)
{
  if (!filename || width <= 0 || height <= 0) return false;

  const size_t expectedSize =
    static_cast<size_t>(width) * static_cast<size_t>(height) * 3;

  if (rgb.size() < expectedSize) return false;

  return stbi_write_png(filename,
                        width,
                        height,
                        3,
                        rgb.data(),
                        width * 3) != 0;
}

bool WritePngRgba(const char* filename,
                  int width,
                  int height,
                  const std::vector<unsigned char>& rgba)
{
  if (!filename || width <= 0 || height <= 0) return false;

  const size_t expectedSize =
    static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

  if (rgba.size() < expectedSize) return false;

  return stbi_write_png(filename,
                        width,
                        height,
                        4,
                        rgba.data(),
                        width * 4) != 0;
}

bool EncodeJpegRgba(int width,
                    int height,
                    const std::vector<unsigned char>& rgba,
                    int quality,
                    std::vector<unsigned char>& jpeg)
{
  if (width <= 0 || height <= 0) return false;
  const size_t expectedSize =
    static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  if (rgba.size() < expectedSize) return false;

  jpeg.clear();
  // stb ignores alpha for four-component JPEG input.
  const int encoded = stbi_write_jpg_to_func(AppendEncodedBytes,
                                              &jpeg,
                                              width,
                                              height,
                                              4,
                                              rgba.data(),
                                              std::clamp(quality, 1, 100));
  if (!encoded) jpeg.clear();
  return encoded != 0 && !jpeg.empty();
}
