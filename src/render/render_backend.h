#pragma once

#include <memory>
#include <cstddef>
#include <string_view>

struct ProjectionPreviewUIState;
struct RenderFrameState;
struct RenderSceneData;
struct RgbImage;
class VulkanContext;

struct RenderBackendMemoryInfo {
  bool gpuAvailableKnown = false;
  std::size_t gpuAvailableBytes = 0;
};

struct RenderBackendTimingInfo {
  bool volumeGpuTimeKnown = false;
  double volumeGpuMs = 0.0;
  bool volumeWallLatencyKnown = false;
  double volumeWallLatencyMs = 0.0;
  bool volumeCacheUsed = false;
  bool volumeCacheUpdated = false;
  bool volumeCacheHit = false;
  double volumeCacheScale = 1.0;
};

struct RenderBackendCapabilities {
  bool particles = false;
  bool particleLod = false;
  bool velocityField = false;
  bool instancedObjects = false;
  bool lines = false;
  bool polyhedra = false;
  bool colorbar = false;
  bool gizmos = false;
  bool projectionPreview = false;
  bool isoContour = false;
  bool volumeRendering = false;
  bool particleFrameCache = false;
  bool volumeFrameCache = false;
  bool gpuMemoryQuery = false;
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
  virtual RenderBackendCapabilities capabilities() const { return {}; }
  virtual bool isSoftwareRenderer() const { return false; }
  virtual RenderBackendMemoryInfo queryMemoryInfo() const { return {}; }
  virtual RenderBackendTimingInfo queryTimingInfo() const { return {}; }
};

enum class RenderBackendKind {
  OpenGL,
  Null,
  Vulkan
};

RenderBackendKind ParseRenderBackendKind(std::string_view name,
                                         RenderBackendKind fallback);
RenderBackendKind DefaultRenderBackendKind();
std::unique_ptr<RenderBackend> CreateRenderBackend(RenderBackendKind kind);

std::unique_ptr<RenderBackend> CreateOpenGLRenderBackend();
std::unique_ptr<RenderBackend> CreateNullRenderBackend();
std::unique_ptr<RenderBackend> CreateVulkanRenderBackend();
std::unique_ptr<RenderBackend> CreateVulkanRenderBackend(VulkanContext& context);
