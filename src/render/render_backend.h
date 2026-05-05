#pragma once

#include <memory>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

struct ProjectionPreviewUIState;
struct RenderFrameState;
struct RenderSceneData;
struct RgbImage;
class MetalContext;
class VulkanContext;

struct RenderBackendMemoryInfo {
  bool gpuAvailableKnown = false;
  std::size_t gpuAvailableBytes = 0;
};

struct RenderBackendTimingInfo {
  struct ParticleGpuLodLevelStats {
    std::uint32_t visited = 0;
    std::uint32_t proxy = 0;
    std::uint32_t leaf = 0;
    std::uint32_t expanded = 0;
    float minProjectedPx = 0.0f;
    float avgProjectedPx = 0.0f;
    float maxProjectedPx = 0.0f;
  };
  static constexpr std::size_t kMaxParticleGpuLodLevels = 32;
  bool particleDrawActive = false;
  bool particleDrawCacheHit = false;
  bool particleDrawWallTimeKnown = false;
  double particleDrawWallMs = 0.0;
  double particleDrawRefreshHz = 0.0;
  bool particleGpuLodActive = false;
  bool particleGpuLodUpdated = false;
  bool particleGpuLodCacheHit = false;
  bool particleGpuLodWallTimeKnown = false;
  double particleGpuLodWallMs = 0.0;
  double particleGpuLodRefreshHz = 0.0;
  bool particleGpuLodDrawWallTimeKnown = false;
  double particleGpuLodDrawWallMs = 0.0;
  double particleGpuLodDrawRefreshHz = 0.0;
  bool particleGpuLodIcbGenerationTimeKnown = false;
  double particleGpuLodIcbGenerationMs = 0.0;
  bool particleGpuLodIcbDrawTimeKnown = false;
  double particleGpuLodIcbDrawMs = 0.0;
  bool particleGpuLodNormalDrawTimeKnown = false;
  double particleGpuLodNormalDrawMs = 0.0;
  bool particleGpuLodStatsKnown = false;
  std::uint64_t particleGpuLodVisitedNodes = 0;
  std::uint64_t particleGpuLodFrustumCulledNodes = 0;
  std::uint64_t particleGpuLodAcceptedProxyNodes = 0;
  std::uint64_t particleGpuLodAcceptedLeafRanges = 0;
  std::uint64_t particleGpuLodLeafParticleCount = 0;
  std::uint64_t particleGpuLodExpandedNodes = 0;
  std::uint64_t particleGpuLodAppendedChildren = 0;
  std::uint64_t particleGpuLodProxyCount = 0;
  std::uint64_t particleGpuLodLeafRangeCount = 0;
  std::uint64_t particleGpuLodMergedLeafRangeCount = 0;
  std::uint64_t particleGpuLodGeneratedDrawCommands = 0;
  std::uint32_t particleGpuLodMaxLeafCount = 0;
  std::uint32_t particleGpuLodLevelCount = 0;
  std::array<ParticleGpuLodLevelStats, kMaxParticleGpuLodLevels>
    particleGpuLodLevels{};
  bool volumeGpuTimeKnown = false;
  double volumeGpuMs = 0.0;
  bool volumeWallLatencyKnown = false;
  double volumeWallLatencyMs = 0.0;
  bool volumeCacheUsed = false;
  bool volumeCacheUpdated = false;
  bool volumeCacheHit = false;
  double volumeCacheScale = 1.0;
};

struct RenderBackendVolumeStats {
  bool known = false;
  int width = 0;
  int height = 0;
  int sampleStep = 1;
  double sampledRays = 0.0;
  double rootHitFraction = 0.0;
  double avgNodeVisitsPerRay = 0.0;
  double avgChildHitsPerRay = 0.0;
  double avgLeafStopsPerRay = 0.0;
  double avgLodStopsPerRay = 0.0;
  double nodeVisits = 0.0;
  double childHits = 0.0;
  double leafStops = 0.0;
  double lodStops = 0.0;
};

struct RenderBackendCapabilities {
  bool particles = false;
  bool particleLod = false;
  bool particleGpuLod = false;
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
  virtual RenderBackendVolumeStats queryVolumeStats(int sampleStep) { (void)sampleStep; return {}; }
  virtual void waitIdle() {}
};

enum class RenderBackendKind {
  OpenGL,
  Null,
  Vulkan,
  Metal
};

RenderBackendKind ParseRenderBackendKind(std::string_view name,
                                         RenderBackendKind fallback);
RenderBackendKind DefaultRenderBackendKind();
std::unique_ptr<RenderBackend> CreateRenderBackend(RenderBackendKind kind);

std::unique_ptr<RenderBackend> CreateOpenGLRenderBackend();
std::unique_ptr<RenderBackend> CreateNullRenderBackend();
std::unique_ptr<RenderBackend> CreateVulkanRenderBackend();
std::unique_ptr<RenderBackend> CreateVulkanRenderBackend(VulkanContext& context);
std::unique_ptr<RenderBackend> CreateMetalRenderBackend(MetalContext& context);
