#pragma once

#include "render/render_backend.h"

#include <cstdint>

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

class OpenGLRenderBackend final : public RenderBackend {
public:
  void init() override;
  void destroy() override;
  void render(const RenderFrameState& frame,
              const RenderSceneData& scene) override;

  void updateProjectionPreview(const RgbImage& image) override;
  ProjectionPreviewUIState makeProjectionPreviewUIState() const override;

private:
  struct UploadedVersions {
    std::uint64_t particles = 0;
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
};
