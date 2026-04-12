#pragma once

#include "core/tracking_vector.h"
#include "interaction/camera.h"
#include "ui_state.h"
#include "scene_manager.h"
#include "particle_visual_config.h"
#include "overlay_state.h"
#include "analysis_state.h"
#include "render/gizmo_renderer.h"
#include "interaction/interaction_utils.h"

class ParticleArray;
class FileInfo;
class WindowContext;

#include "app_services.h"

#ifdef ISO_CONTOUR
struct IsoContourGeometryState {
  TrackingVector<float> verts;
  TrackingVector<unsigned> inds;

  void clear() {
    verts.clear();
    inds.clear();
  }
};
#endif

struct AppDataState {
  ParticleArray* particles = nullptr;
  FileInfo* fileInfo = nullptr;
};

struct AppViewState {
  CameraContext camera;
  ParticleVisualConfig particleVisual;
};

struct AppRuntimeState {
  InteractionState interaction;
  SettingsRuntimeState settings;
  RenderRuntimeState render;
};

struct AppDerivedState {
  SceneManagers scene;
  OverlayState overlay;
  AnalysisState analysis;
#ifdef ISO_CONTOUR
  IsoContourGeometryState isoContour;
#endif
};

struct AppState {
  AppServices services;
  AppDataState data;
  AppViewState view;
  AppRuntimeState runtime;
  AppDerivedState derived;
};

struct CallbackContext {
  AppState* app = nullptr;
  WindowContext* window = nullptr;
};
