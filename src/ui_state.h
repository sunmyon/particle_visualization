#pragma once

#include <glm/vec3.hpp>

struct SettingsRuntimeState {
  float radiusCullingSphere = 1.0f;
  float minZoom = 0.1f;
  float maxZoom = 500.0f;
  float crossSize = 0.05f;

  float queryRadius = 0.5f;
  int nqueryparticles = 0;
  bool flagShowSinkIDs = false;
  float moveThreshold = 0.05f;
  glm::vec3 lastCameraPos = glm::vec3(0.0f);
};

struct RenderLayerState {
  bool show = false;
  bool cpuUpdated = false;
  float opacity = 1.0f;
};

struct OverlayRuntimeState {
  bool showCoordinates = true;
  bool showCrossGizmo = true;
  bool showColorbar = true;
  float crossSize = 0.05f;
};

struct VelocityRenderState : RenderLayerState {
  VelocityRenderState() { show = false; }

  int subtraction = 100;
  float arrowScale = 1.0f;
  bool useLogScale = false;
};

struct RenderRuntimeState {
  RenderLayerState lines;
  RenderLayerState disks;
  RenderLayerState cubes;
  RenderLayerState ellipsoids;
  RenderLayerState isocontour;
  
  RenderLayerState polyhedra;
  RenderLayerState cuboids;
  RenderLayerState cuboidAnnotations;

  VelocityRenderState velocity;
  OverlayRuntimeState overlay;  
  
  struct Volume {
    bool show = false;
    bool cpuUpdated = false;
    int flagRT = 0;
    int kernelMode = 0;
    float gaussNSigma = 1.0f;
    float enlargeKernel = 1.0f;
    int lodMode = 0;
    float pxThreshold = 1.0f;
    float tauMax = 1.0f;
    float rtDownscale = 1.0f;
  } volume;
};

