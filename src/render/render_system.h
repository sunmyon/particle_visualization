#pragma once

#include "render/render_programs.h"
#include "render/render_resources.h"
#include "render/particle_renderer.h"
#include "render/object_renderer.h"
#include "render/gizmo_renderer.h"
#include "render/field_renderer.h"
#include "projection_preview_gl.h"

struct RenderSystem {
  RenderPrograms programs;
  RenderResources resources;

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
