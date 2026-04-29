#pragma once
struct RenderLayerState {
  bool show = false;
  bool cpuUpdated = false;
  bool gpuUpdated = false;
  float opacity = 1.0f;
};

struct ColorbarLayoutSettings {
  float width = 400.0f;
  float height = 40.0f;
  float margin = 40.0f;
};

struct ColorbarRenderState : RenderLayerState {
  ColorbarRenderState() { show = true; }
  
  int sourceParticleType = 0;
  int numTicks = 5;
  ColorbarLayoutSettings layout;
};

struct CrossGizmoRenderState : RenderLayerState {
  CrossGizmoRenderState() { show = true; }
  float size = 0.05f;
};

struct CoordAxesRenderState : RenderLayerState {
  CoordAxesRenderState() { show = true; }
};

struct ParticleLabelRenderState : RenderLayerState {
  ParticleLabelRenderState() { show = false; }

  float queryRadius = 0.0f;
  float moveThreshold = 0.0f;
  int maxLabels = 50;
  glm::vec3 lastCameraPos{0.0f};
};

struct VelocityRenderState : RenderLayerState {
  VelocityRenderState() { show = false; }

  int subtraction = 100;
  float arrowScale = 1.0f;
  bool useLogScale = false;
};

#ifdef VOLUME_RENDERING
struct VolumeRenderState : RenderLayerState {
  VolumeRenderState() { show = false; }

  int debugMode = 0;
  float pixelThreshold = 2.0f;
  float tauMax = 1.0f;
  float stepBias = 0.0f;
  float skipEpsilon = 1.0e-4f;
};
#endif

struct RenderRuntimeState {
  RenderLayerState lines;
  RenderLayerState disks;
  RenderLayerState cubes;
  RenderLayerState ellipsoids;
  RenderLayerState isocontour;

#ifdef VOLUME_RENDERING
  VolumeRenderState volume;
#endif
  
  RenderLayerState polyhedra;
  RenderLayerState cuboids;
  RenderLayerState cuboidAnnotations;

  VelocityRenderState velocity;

  CrossGizmoRenderState crossGizmo;
  CoordAxesRenderState coordAxes;
  ColorbarRenderState colorbar;
  ParticleLabelRenderState particleLabels;
};
