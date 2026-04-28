#pragma once
struct ProjectionPreviewUIState {
  void* textureId = nullptr;   // For ImTextureID.
  int width = 0;
  int height = 0;
  bool valid = false;
};
