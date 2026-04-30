#pragma once

#include <vector>

#include <glm/vec3.hpp>

#include "render/particle_lod.h"

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

struct RenderSchedulingState {
  bool responsiveInteraction = true;
  bool skipVolumeWhileInteracting = true;
  bool cacheParticleFrames = true;
  bool cacheVolumeFrames = true;
  float volumeFrameCacheScale = 1.0f;
  bool autoParticleLodOnSoftwareRenderer = true;
  ParticleLodSettings particleLod;
  float settleDelaySeconds = 0.15f;
  bool interactionActive = false;
};

#ifdef VOLUME_RENDERING
inline constexpr int kMaxVolumeTransferComponents = 16;

struct VolumeTransferFunctionComponent {
  int type = 0; // 0=Gaussian, 1=Box, 2=Triangle.
  float center = 1.0f;
  float width = 1.0f;
  float amplitude = 0.0f;
  bool logDomain = true;
};

struct VolumeRenderState : RenderLayerState {
  VolumeRenderState() { show = false; }

  int debugMode = 0;
  float pixelThreshold = 2.0f;
  float tauMax = 1.0f;
  float stepBias = 0.0f;
  float skipEpsilon = 1.0e-4f;
  glm::vec3 baseColor{0.6f, 0.7f, 1.0f};
  int colorMode = 0; // 0=fixed color, 1=procedural heat.
  float tfValueMin = 1.0e-6f;
  float tfValueMax = 1.0f;
  float tfSigmaScale = 1.0f;
  float tfMaxSigma = 0.0f;
  bool tfLogScale = true;
  std::vector<VolumeTransferFunctionComponent> tfComponents;
};
#endif

struct RenderRuntimeState {
  RenderSchedulingState scheduling;

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
