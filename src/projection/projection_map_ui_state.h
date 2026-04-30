#pragma once

#include <cstdint>

struct ProjectionPreviewUIState {
  void* textureId = nullptr;   // For ImTextureID.
  int width = 0;
  int height = 0;
  uint64_t version = 0;
  bool valid = false;
};
