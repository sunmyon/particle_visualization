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

struct DiskAnalysisRequestState {
  int targetParticleId = 0;
  bool runRequested = false;
  bool clearRequested = false;
};

struct DiskAnalysisBatchRequestState {
  bool runRequested = false;
  char inputFile[255] = "binary_fragmentation_ellipticity_all_w_mode.txt";
  char outputFile[255] = "binary_fragmentation_disks.txt";
};

struct EllipsoidAnalysisRequestState {
  int particleId1 = 0;
  int particleId2 = 0;
  bool runRequested = false;
  bool clearRequested = false;
};

struct EllipsoidAnalysisBatchRequestState {
  bool runRequested = false;
  char inputFile[255] = "binary_fragmentation.txt";
  char outputFile[255] = "binary_fragmentation_output.txt";
};

struct StreamlinePreviewRequestState {
  float seedCenter[3] = {0.f, 0.f, 0.f};
  float seedSize[3]   = {100.f, 100.f, 100.f};
  float opacity       = 0.1f;

  bool updateRequested = false;
  bool clearRequested  = false;
};

struct StreamlineBuildRequestState {
  int nSeeds = 1;

  bool limitRegion = false;
  float regionCenter[3] = {0.f, 0.f, 0.f};
  float regionSize[3]   = {0.f, 0.f, 0.f};

  bool runRequested   = false;
  bool clearRequested = false;
};

struct StellarDensityRequestState {
  bool selectedTypes[6] = { false, false, false, true, true, true };
  bool overwriteHsml = false;
  bool runRequested = false;
};

#ifdef ISO_CONTOUR
struct IsoContourRequestState {
  float isoLevel = 0.0f;
  int maxTreeLevel = 15;
  QuantityId selectedQuantity = QuantityId::Density;
  bool runRequested = false;
  bool clearRequested = false;
};
#endif

struct AnalysisRequestRuntimeState {
  StellarDensityRequestState stellarDensity;

  DiskAnalysisRequestState disk;
  DiskAnalysisBatchRequestState diskBatch;

  EllipsoidAnalysisRequestState ellipsoid;
  EllipsoidAnalysisBatchRequestState ellipsoidBatch;

#ifdef ISO_CONTOUR
  IsoContourRequestState isoContour;
#endif

#ifdef STREAM_LINE
  StreamlinePreviewRequestState streamlinePreview;
  StreamlineBuildRequestState streamlineBuild;
#endif
};

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
  AnalysisRequestRuntimeState analysis;
};

struct AppDerivedState {
  SceneManagers scene;
  OverlayState overlay;
  AnalysisDerivedState analysis;
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
