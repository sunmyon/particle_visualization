#pragma once

#include <memory>
#include <cstddef>
#include <string_view>

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

enum class RenderBackendKind {
  OpenGL,
  Null
};

RenderBackendKind ParseRenderBackendKind(std::string_view name,
                                         RenderBackendKind fallback);
RenderBackendKind DefaultRenderBackendKind();
std::unique_ptr<RenderBackend> CreateRenderBackend(RenderBackendKind kind);

std::unique_ptr<RenderBackend> CreateOpenGLRenderBackend();
std::unique_ptr<RenderBackend> CreateNullRenderBackend();
