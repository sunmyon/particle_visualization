#pragma once

#include <memory>
#include <cstddef>

struct ProjectionPreviewUIState;
struct RenderFrameState;
struct RenderSceneData;
struct RgbImage;

struct RenderBackendMemoryInfo {
  bool gpuAvailableKnown = false;
  std::size_t gpuAvailableBytes = 0;
};

class RenderBackend {
public:
  virtual ~RenderBackend() = default;

  virtual void init() = 0;
  virtual void destroy() = 0;
  virtual void render(const RenderFrameState& frame,
                      const RenderSceneData& scene) = 0;

  virtual void updateProjectionPreview(const RgbImage& image) = 0;
  virtual ProjectionPreviewUIState makeProjectionPreviewUIState() const = 0;
  virtual bool isSoftwareRenderer() const { return false; }
  virtual RenderBackendMemoryInfo queryMemoryInfo() const { return {}; }
};

std::unique_ptr<RenderBackend> CreateOpenGLRenderBackend();
