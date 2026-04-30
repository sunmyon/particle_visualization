#pragma once

#include "render/render_backend.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "render/particle_visual_config.h"
#include "render/opengl/field_renderer.h"
#include "render/opengl/gizmo_renderer.h"
#include "render/opengl/object_renderer.h"
#include "render/opengl/particle_renderer.h"
#include "render/opengl/projection_preview_gl.h"
#include "render/opengl/render_programs.h"

#ifdef VOLUME_RENDERING
#include "render/opengl/adaptive_volume_renderer.h"
#endif

#ifdef VOLUME_RENDERING
struct OpenGLVolumeFrameCache {
  GLuint framebuffer = 0;
  GLuint texture = 0;
  GLuint vao = 0;
  int width = 0;
  int height = 0;
  std::uint64_t volumeVersion = 0;
  AdaptiveVolumeDrawParams params;
  bool valid = false;
};
#endif

struct OpenGLParticleFrameCache {
  GLuint framebuffer = 0;
  GLuint colorTexture = 0;
  GLuint depthRenderbuffer = 0;
  GLuint vao = 0;
  int width = 0;
  int height = 0;
  std::uint64_t particlesVersion = 0;
  glm::mat4 model{1.0f};
  glm::mat4 view{1.0f};
  glm::mat4 projection{1.0f};
  ParticleVisualConfig visualConfig;
  bool valid = false;
};

class OpenGLRenderBackend final : public RenderBackend {
public:
  void init() override;
  void destroy() override;
  void render(const RenderFrameState& frame,
              const RenderSceneData& scene) override;

  void updateProjectionPreview(const RgbImage& image) override;
  ProjectionPreviewUIState makeProjectionPreviewUIState() const override;
  RenderBackendCapabilities capabilities() const override;
  bool isSoftwareRenderer() const override { return softwareRenderer_; }
  RenderBackendMemoryInfo queryMemoryInfo() const override;
  RenderBackendTimingInfo queryTimingInfo() const override { return timing_; }

private:
#ifdef VOLUME_RENDERING
  void pollVolumeTimingFence();
  void markVolumeTimingFence();
#endif

  struct UploadedVersions {
    std::uint64_t particles = 0;
    std::uint64_t stressParticles = 0;
    std::uint64_t particleLod = 0;
    std::uint64_t particleLodStress = 0;
    std::uint64_t velocity = 0;
    std::uint64_t cubes = 0;
    std::uint64_t ellipsoids = 0;
    std::uint64_t disks = 0;
    std::uint64_t cuboids = 0;
    std::uint64_t lines = 0;
    std::uint64_t polyhedra = 0;
#ifdef ISO_CONTOUR
    std::uint64_t isoContour = 0;
#endif
#ifdef VOLUME_RENDERING
    std::uint64_t volume = 0;
#endif
  };

  RenderPrograms programs_;
  UploadedVersions uploaded_;

  ParticleRenderer particle_;
  ParticleRenderer particleStress_;
  ParticleRenderer particleLod_;
  ParticleRenderer particleLodStress_;
  OpenGLParticleFrameCache particleFrameCache_;
  VelocityFieldRenderer velocity_;

  EllipsoidRenderer ellipsoid_;
  DiskRenderer disk_;
  CubeRenderer cube_;
  CuboidRenderer cuboid_;
  LineRenderer line_;
  PolyhedronRenderer polyhedron_;
#ifdef ISO_CONTOUR
  IsoContourRenderer isocontour_;
#endif
#ifdef VOLUME_RENDERING
  AdaptiveVolumeRenderer volume_;
  OpenGLVolumeFrameCache volumeFrameCache_;
#endif

  CrossGizmoRenderer crossGizmo_;
  CoordAxesRenderer coordAxes_;
  ColorbarRenderer colorbar_;
  ColorbarLabelRenderer colorbarLabels_;

  ProjectionPreviewGL preview_;

  std::string glVendor_;
  std::string glRenderer_;
  bool softwareRenderer_ = false;
  bool hasNvxGpuMemoryInfo_ = false;
  bool hasAtiMeminfo_ = false;
  RenderBackendTimingInfo timing_;
#ifdef VOLUME_RENDERING
  GLsync volumeTimingFence_ = nullptr;
  std::chrono::steady_clock::time_point volumeTimingWallStart_;
  bool volumeTimingWallStartValid_ = false;
#endif
};
