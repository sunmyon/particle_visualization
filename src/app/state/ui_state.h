#pragma once
#include <array>
#include <vector>

#include "app/state/tracking_view_state.h"
#include "core/quantity.h"

struct SettingsStellarDensityEdit {
  bool selectedTypes[6] = { false, false, false, true, true, true };
  bool overwriteHsml = false;
  bool computeClicked = false;
};

#ifdef CLUMP_DATA_READ
struct SettingsClumpBatchEdit {
  int method = 0;
  int nSnapshots = 10;
  char outputFileName[255] = "clump_data.hdf5";
  char outputFolderPath[255] = "./output/";
  bool generateClicked = false;
  bool cancelClicked = false;
};
#endif

struct SettingsDiskAnalysisEdit {
  int targetParticleId = 0;
  bool findClicked = false;
  bool clearClicked = false;
};

struct SettingsDiskBatchEdit {
  char inputFile[255] = "binary_fragmentation_ellipticity_all_w_mode.txt";
  char outputFile[255] = "binary_fragmentation_disks.txt";
  bool runClicked = false;
  bool cancelClicked = false;
};

struct SettingsEllipsoidAnalysisEdit {
  int particleId1 = 0;
  int particleId2 = 0;
  bool fitClicked = false;
  bool clearClicked = false;
};

struct SettingsEllipsoidBatchEdit {
  char inputFile[255] = "binary_fragmentation.txt";
  char outputFile[255] = "binary_fragmentation_output.txt";
  bool runClicked = false;
  bool cancelClicked = false;
};

#ifdef ISO_CONTOUR
struct SettingsIsoContourEdit {
  float isoLevel = 0.0f;
  int maxTreeLevel = 15;
  QuantityId selectedQuantity = QuantityId::Density;
  bool buildClicked = false;
  bool clearClicked = false;
};
#endif

#ifdef STREAM_LINE
struct SettingsStreamlinePreviewEdit {
  float seedCenter[3] = {0.f, 0.f, 0.f};
  float seedSize[3] = {100.f, 100.f, 100.f};
  float opacity = 0.1f;
  bool updateClicked = false;
  bool clearClicked = false;
};

struct SettingsStreamlineBuildEdit {
  int nSeeds = 1;
  int fieldSource = 0; // 0: velocity, 1: B field
  int maxSteps = 1000000;
  float stepScale = 0.15f;
  float thetaMaxDegrees = 10.0f;
  bool useManualSeed = false;
  std::vector<std::array<float, 3>> manualSeeds{{0.f, 0.f, 0.f}};
  bool limitRegion = false;
  float regionCenter[3] = {0.f, 0.f, 0.f};
  float regionSize[3] = {0.f, 0.f, 0.f};
  bool buildClicked = false;
  bool clearClicked = false;
};
#endif

#ifdef VOLUME_RENDERING
struct SettingsVolumeRenderingEdit {
  QuantityId selectedQuantity = QuantityId::Density;
  int minParticlesPerLeaf = 64;
  int maxTreeLevel = 16;
  float sigmaScale = 1.0f;
  std::vector<float> sigmaLut;
  float sigmaLutValueMin = 1.0e-6f;
  float sigmaLutValueMax = 1.0f;
  bool sigmaLutLogSample = true;
  bool logScale = true;
  bool autoRange = true;
  float valueMin = 1.0e-6f;
  float valueMax = 1.0f;
  bool balanceTree = false;
  bool buildClicked = false;
  bool clearClicked = false;
};
#endif

#ifdef PYTHON_BRIDGE
struct SettingsPythonBridgeEdit {
  bool launchClicked = false;
  bool shutdownClicked = false;
  bool openBrowserClicked = false;
};
#endif

struct SettingsProjectionMovieEdit {
  int nSnapshots = 10;
  char outputFileFormat[255] = "image_%04d.png";
  char outputFolderPath[255] = "./output";
  char outputMovieName[255] = "output.mp4";

  bool faceOn = false;
  bool alignToAngularMomentum = false;
  AngularMomentumViewMode amViewMode = AngularMomentumViewMode::FaceOn;
  float amRadius = 0.0f;
  bool amSubtractBulkVelocity = true;
  std::array<bool, 6> amUseType = {true, true, true, true, true, true};
  bool amKeepSignContinuity = true;

  bool followSinkCenter = false;
  bool followMostMassiveSink = false;
  int particleIdCenter = 0;
  bool useMassCenter = false;
  float massCenterRadius = 0.0f;
  float massCenterMinDensity = 0.0f;

  bool restoreCameraOnFinish = true;
  bool generateClicked = false;
  bool cancelClicked = false;
};

struct SettingsAnalysisEditState {
  SettingsStellarDensityEdit stellarDensity;
  bool stellarDensityDirty = false;

#ifdef CLUMP_DATA_READ
  SettingsClumpBatchEdit clumpBatch;
  bool clumpBatchDirty = false;
#endif

  SettingsDiskAnalysisEdit disk;
  bool diskDirty = false;
  SettingsDiskBatchEdit diskBatch;
  bool diskBatchDirty = false;

  SettingsEllipsoidAnalysisEdit ellipsoid;
  bool ellipsoidDirty = false;
  SettingsEllipsoidBatchEdit ellipsoidBatch;
  bool ellipsoidBatchDirty = false;

#ifdef ISO_CONTOUR
  SettingsIsoContourEdit isoContour;
  bool isoContourDirty = false;
#endif

#ifdef STREAM_LINE
  SettingsStreamlinePreviewEdit streamlinePreview;
  bool streamlinePreviewDirty = false;
  SettingsStreamlineBuildEdit streamlineBuild;
  bool streamlineBuildDirty = false;
#endif

#ifdef VOLUME_RENDERING
  SettingsVolumeRenderingEdit volume;
  bool volumeDirty = false;
#endif

#ifdef PYTHON_BRIDGE
  SettingsPythonBridgeEdit py;
  bool pyDirty = false;
#endif

  SettingsProjectionMovieEdit projectionMovie;
  bool projectionMovieDirty = false;
};

struct SettingsUIState {
  int analysisMode = 0;
  int renderingMode = 0;
  SettingsAnalysisEditState analysisEdit;
};
