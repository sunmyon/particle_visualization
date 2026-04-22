#pragma once
#include <glm/vec3.hpp>
#include "app/normalization_config.h"
#include "app/input_filter_config.h"
#include "app/view_filter_config.h"
#include "app/tracking_view_state.h"

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
#include "core/quantity.h"
struct IsoContourRequestState {
  float isoLevel = 0.0f;
  int maxTreeLevel = 15;
  QuantityId selectedQuantity = QuantityId::Density;
  bool runRequested = false;
  bool clearRequested = false;
};
#endif

struct ClumpRequestState {
  bool openRequested = false;
  bool runRequested = false;
};

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

#ifdef PYTHON_BRIDGE
struct PythonBridgeRequestState {
  bool launchRequested = false;
  bool shutdownRequested = false;
  bool openBrowserRequested = false;
};

struct PythonBridgeViewState {
  bool available = false;
  bool launched = false;
  int port = -1;
  std::string url;
  std::string token;
  std::string lastError;
};

struct PythonBridgeRuntimeState {
  PythonBridgeRequestState request;
  PythonBridgeViewState view;
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

#ifdef CLUMP_DATA_READ
  ClumpBatchRequestState clumpBatch;
#endif

#ifdef PYTHON_BRIDGE
  PythonBridgeRuntimeState py;
#endif
  
  ProjectionMovieRequestState projectionMovie;
};

struct CameraPlacementRequestState {
  bool setCenterRequested = false;
  bool setProjectionRequested = false;
  bool applyCullingRequested = false;
  bool clearCullingRequested = false;

  float centerInput[3] = {0.0f, 0.0f, 0.0f};
  bool inputIsOriginal = true;

  int currentView = 0;
  float rollAngle = 0.0f;
};

struct FileNavigationRequestState {
  bool applySkipStepRequested = false;
  bool loadSelectedSnapshotRequested = false;
  bool loadPreviousRequested = false;
  bool loadNextRequested = false;
  bool loadBatchRequested = false;
  bool reloadRequested = false;
  bool openFormatDialogRequested = false;
  bool generateTestDataRequested = false;
};

struct FileNavigationRuntimeState {
  int tempSkipStep = 1;
  FileNavigationRequestState request;
};

struct SnapshotPostprocessState {
  bool refreshTree = false;
  bool refreshCulling = false;
  bool refreshTopParticles = false;
  bool applyTrackingToCamera = false;
};

struct SettingsRuntimeState {
  float minZoom = 0.1f;
  float maxZoom = 500.0f;

  SnapshotPostprocessState snapshotPostprocess;
  ViewFilterConfig viewFilter;
  InputFilterConfig inputFilter;
  NormalizationContext normalization;
  FileNavigationRuntimeState fileNavigation;
  CameraPlacementRequestState cameraPlacement;
  TrackingTargetState tracking;
};

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

  CrossGizmoRenderState crossGizmo;
  CoordAxesRenderState coordAxes;
  ColorbarRenderState colorbar;
  ParticleLabelRenderState particleLabels;
  
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

