#pragma once

#include "core/tracking_vector.h"
#include "interaction/camera.h"
#include "scene_manager.h"
#include "particle_visual_config.h"
#include "runtime_state.h"
#include "overlay_state.h"
#include "analysis_state.h"
#include "ui_state.h"
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

struct ClumpBatchRequestState {
  int method = 0;                  // 0: FOF, 1: Dendrogram
  int nSnapshots = 10;
  char outputFileName[255] = "clump_data.hdf5";
  char outputFolderPath[255] = "./output/";
  bool runRequested = false;
};

struct ProjectionMovieRequestState {
  int nSnapshots = 10;
  char outputFileFormat[255] = "image_%04d.png";
  char outputFolderPath[255] = "./output";
  char outputMovieName[255] = "output.mp4";

  bool faceOn = false;
  bool followSinkCenter = false;
  bool followMostMassiveSink = false;
  int particleIdCenter = 0;
  bool useMassCenter = false;
  float massCenterRadius = 0.0f;
  float massCenterMinDensity = 0.0f;

  bool runRequested = false;
};

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

#ifdef CLUMP_DATA_READ
  ClumpBatchRequestState clumpBatch;
#endif

  ProjectionMovieRequestState projectionMovie;
};

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
  ToolWindowUIState toolWindows;
};

struct AppDerivedState {
  SceneManagers scene;
  OverlayState overlay;
  AnalysisDerivedState analysis;
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
