#include <cstdlib>
#include <iostream>
#include <vector>

#include "image/image_io.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

int main()
{
  constexpr int width = 64;
  constexpr int height = 48;
  std::vector<unsigned char> rgba(width * height * 4);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t offset = static_cast<std::size_t>(y * width + x) * 4;
      rgba[offset] = static_cast<unsigned char>(x * 4);
      rgba[offset + 1] = static_cast<unsigned char>(y * 5);
      rgba[offset + 2] = static_cast<unsigned char>((x + y) * 2);
      rgba[offset + 3] = 255;
    }
  }

  std::vector<unsigned char> jpeg;
  if (!EncodeJpegRgba(width, height, rgba, 80, jpeg)) {
    std::cerr << "JPEG encoding failed\n";
    return EXIT_FAILURE;
  }

  int decodedWidth = 0;
  int decodedHeight = 0;
  int channels = 0;
  stbi_uc* decoded = stbi_load_from_memory(jpeg.data(),
                                           static_cast<int>(jpeg.size()),
                                           &decodedWidth,
                                           &decodedHeight,
                                           &channels,
                                           4);
  const bool valid = decoded && decodedWidth == width && decodedHeight == height;
  stbi_image_free(decoded);
  if (!valid) {
    std::cerr << "JPEG round trip produced invalid dimensions\n";
    return EXIT_FAILURE;
  }

  std::cout << "JPEG round trip: " << rgba.size() << " -> " << jpeg.size()
            << " bytes\n";
  return EXIT_SUCCESS;
}
