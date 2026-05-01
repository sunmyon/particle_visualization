#include "image/image_io.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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
