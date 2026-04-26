#pragma once

#include <cstdint>
#include <vector>

enum class RenderedFrameFormat {
  None,
  RGBA8
};

struct RenderedFrame {
  uint64_t frameId = 0;
  int width = 0;
  int height = 0;
  RenderedFrameFormat format = RenderedFrameFormat::None;
  std::vector<uint8_t> pixels;

  bool valid() const {
    return format != RenderedFrameFormat::None &&
           width > 0 &&
           height > 0 &&
           !pixels.empty();
  }
};
