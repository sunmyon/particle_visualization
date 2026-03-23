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

  bool showVelocityVectors = false;
  int velocitySubtraction = 1;
  float arrowScale = 1.0f;
  bool useVelocityArrowLogScale = false;

  bool flagHideAllParticles = false;
};

struct RenderRuntimeState {
  bool showDisks = false;
  bool showEllipsoid = false;
  bool showStreamLine = false;
  bool flagStreamDirty = false;

  bool flagCubesDirty = false;
  bool showIsocontour = false;
  bool showVolumeRendering = false;
  bool flagUpdateRendering = false;

  float diskOpacity = 1.0f;
  float streamlineOpacity = 0.9f;
  float isoOpacity = 1.0f;
  float isoOpacityEllipsoid = 1.0f;
  
  int flagRT = 0;
  int kernelMode = 0;
  float gaussNSigma = 1.0f;
  float enlargeKernel = 1.0f;
  int lodMode = 0;
  float pxThreshold = 1.0f;
  float tauMax = 1.0f;
  float rtDownscale = 1.0f;
};
