#pragma once

#include "render/render_programs.h"
#include "render/render_resources.h"
#include "render/frame_matrices.h"
#include "render/particle_renderer.h"
#include "render/object_renderer.h"
#include "render/gizmo_renderer.h"
#include "render/field_renderer.h"
#include "projection_preview_gl.h"

#include "app/overlay_state.h"
#include "app/render_runtime_state.h"
#include "interaction/camera.h"
#include "particle_visual_config.h"

struct RenderFrameState {
  FrameMatrices matrices;
  CameraContext camera;
  ParticleVisualConfig particleVisual;
  RenderRuntimeState runtime;
  OverlayState overlay;
  bool valid = false;
};

struct RenderSystem {
  RenderPrograms programs;
  RenderResources resources;
  RenderFrameState frame;

  ParticleRenderer particle;
  VelocityFieldRenderer velocity;

  EllipsoidRenderer ellipsoid;
  DiskRenderer disk;
  CubeRenderer cube;
  CuboidRenderer cuboid;
  LineRenderer line;
  PolyhedronRenderer polyhedron;
#ifdef ISO_CONTOUR
  IsoContourRenderer isocontour;
#endif

  CrossGizmoRenderer crossGizmo;
  CoordAxesRenderer coordAxes;
  ColorbarRenderer colorbar;
  ColorbarLabelRenderer colorbarLabels;

  ProjectionPreviewGL preview;
};
