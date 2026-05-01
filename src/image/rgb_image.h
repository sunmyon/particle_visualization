#pragma once

#include <vector>

#include <cstdint>

struct RgbImage {
  int width = 0;
  int height = 0;
  uint64_t version = 0;
  std::vector<unsigned char> rgb;

  bool valid() const {
    return width > 0 &&
           height > 0 &&
           rgb.size() >= static_cast<size_t>(width) * height * 3;
  }

  void clear() {
    width = 0;
    height = 0;
    version = 0;
    rgb.clear();
  }
};

class ImageCanvas;
RgbImage ToRgbImage(const ImageCanvas& canvas, uint64_t version = 0);
