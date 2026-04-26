#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <glm/vec3.hpp>

#include "core/quantity.h"
#include "app/normalization_config.h"
#include "app/input_filter_config.h"
#include "app/view_filter_config.h"
#include "app/tracking_view_state.h"
#include "app/render_runtime_state.h"
#include "particle_visual_config.h"
#include "projection/projection_map_tool_state.h"
#include "FileIO/file_format_types.h"

enum class SnapshotLoadOwner : uint8_t {
  None = 0,
  ProjectionMovie = 1,
  UserNavigation = 2,
  DiskBatch = 3,
  EllipsoidBatch = 4,
  ClumpBatch = 5,
  ClumpChainProjectionBatch = 6
};

enum class SnapshotLoadKind : uint8_t {
  FileStep,
  GenerateTestData
};

enum class JobStatus : uint8_t {
  Idle = 0,
  Running = 1,
  Completed = 2,
  Cancelled = 3,
  Error = 4
};

struct SnapshotJobRuntimeState {
  JobStatus status = JobStatus::Idle;
  bool cancelRequested = false;

  int savedCurrentStep = 0;
  int beginStep = 0;
  int endStep = 0;
  int nextStep = 0;
  int stepStride = 1;
  int processed = 0;

  bool savedTrackingValid = false;
  TrackingTargetState savedTracking{};

  bool savedCameraValid = false;
  std::array<float, 3> savedCameraPos = {0.f, 0.f, 5.f};
  std::array<float, 3> savedCameraTarget = {0.f, 0.f, 0.f};
  std::array<float, 3> savedCameraUp = {0.f, 1.f, 0.f};
  std::array<float, 4> savedCameraOrientation = {1.f, 0.f, 0.f, 0.f}; // w, x, y, z
  float savedCameraDistance = 5.f;
};

struct SnapshotLoadRequestState {
  bool pending = false;
  int targetStep = 0;
  int priority = 0;
  
  SnapshotLoadKind kind = SnapshotLoadKind::FileStep; 
  SnapshotLoadOwner owner = SnapshotLoadOwner::None;
};

struct SnapshotLoadResultState {
  bool loadedThisFrame = false;
  bool failedThisFrame = false;
  int loadedStep = -1;
  SnapshotLoadOwner owner = SnapshotLoadOwner::None;
  char errorMessage[256] = "";
};

struct SnapshotLoadRuntimeState {
  SnapshotLoadRequestState request;
  SnapshotLoadResultState result;
  bool busy = false;
};

inline void RequestSnapshotLoad(SnapshotLoadRuntimeState& load,
                                SnapshotLoadOwner owner,
                                int targetStep,
                                int priority,
				SnapshotLoadKind kind = SnapshotLoadKind::FileStep)
{
  if (!load.request.pending || priority >= load.request.priority) {
    load.request.pending = true;
    load.request.targetStep = targetStep;
    load.request.priority = priority;
    load.request.kind = kind;
    load.request.owner = owner;
  }
}

inline bool IsSnapshotLoadedFor(const SnapshotLoadRuntimeState& load,
                                SnapshotLoadOwner owner,
                                int step)
{
  return load.result.loadedThisFrame &&
         load.result.owner == owner &&
         load.result.loadedStep == step;
}

inline bool IsSnapshotLoadFailedFor(const SnapshotLoadRuntimeState& load,
                                    SnapshotLoadOwner owner,
                                    int step)
{
  return load.result.failedThisFrame &&
         load.result.owner == owner &&
         load.result.loadedStep == step;
}


struct DiskAnalysisRequestState {
  int targetParticleId = 0;
  bool runRequested = false;
  bool clearRequested = false;
};

struct DiskAnalysisBatchTargetRow {
  int idx = 0;
  int idA = 0;
  int idB = 0;
  int snap = -1;
};

struct DiskAnalysisBatchRequestState {
  bool runRequested = false;
  bool cancelRequested = false;
  char inputFile[255] = "binary_fragmentation_ellipticity_all_w_mode.txt";
  char outputFile[255] = "binary_fragmentation_disks.txt";
};

struct DiskAnalysisBatchRuntimeState {
  SnapshotJobRuntimeState job;
  DiskAnalysisBatchRequestState params;
  std::vector<DiskAnalysisBatchTargetRow> rows;
  int rowCursor = 0;
  int scanOffset = 0;
  bool firstOutput = true;
  bool firstEvolution = true;
  int snapDisk = -1;
  int snapNotDisk = -1;
  double timeDisk = -1.0;
  double timeNotDisk = -1.0;
  double distDisk = 0.0;
  double rDisk1 = 0.0;
  double rDisk2 = 0.0;
  char evolutionOutputFile[255] = "";
};

struct EllipsoidAnalysisRequestState {
  int particleId1 = 0;
  int particleId2 = 0;
  bool runRequested = false;
  bool clearRequested = false;
};

struct EllipsoidAnalysisBatchTargetRow {
  int idx = 0;
  int idA = 0;
  int idB = 0;
  int snap = -1;
};

struct EllipsoidAnalysisBatchRequestState {
  bool runRequested = false;
  bool cancelRequested = false;
  char inputFile[255] = "binary_fragmentation.txt";
  char outputFile[255] = "binary_fragmentation_output.txt";
};

struct EllipsoidAnalysisBatchRuntimeState {
  SnapshotJobRuntimeState job;
  EllipsoidAnalysisBatchRequestState params;
  std::vector<EllipsoidAnalysisBatchTargetRow> rows;
  int rowCursor = 0;
  bool firstOutput = true;
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
  bool cancelRequested = false;
  bool runRequested = false;
};

struct ClumpBatchRuntimeState {
  SnapshotJobRuntimeState job;
  ClumpBatchRequestState params;
};

struct ProjectionMovieRequestState {
  int nSnapshots = 10;
  char outputFileFormat[255] = "image_%04d.png";
  char outputFolderPath[255] = "./output";
  char outputMovieName[255] = "output.mp4";

  bool faceOn = false; // legacy toggle (maps to face-on angular-momentum view)
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
  bool cancelRequested = false;
  bool runRequested = false;
};

struct ProjectionMovieRuntimeState {
  SnapshotJobRuntimeState job;
  ProjectionMovieRequestState params;
  ProjectionMapParams projectionParams;
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

#endif

struct AnalysisRequestState {
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
  PythonBridgeRequestState py;
#endif
  
  ProjectionMovieRequestState projectionMovie;
  ProjectionMapRequestState projectionMapRequest;
};

struct AnalysisViewState {
#ifdef PYTHON_BRIDGE
  PythonBridgeViewState py;
#endif
};

struct AnalysisToolState {
  ProjectionMapToolState projectionMap;
};

struct AnalysisJobRuntimeState {
  DiskAnalysisBatchRuntimeState diskBatch;
  EllipsoidAnalysisBatchRuntimeState ellipsoidBatch;

#ifdef CLUMP_DATA_READ
  ClumpBatchRuntimeState clumpBatch;
#endif

  ProjectionMovieRuntimeState projectionMovie;
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
  bool openHDF5FormatDialogRequested = false;
  bool generateTestDataRequested = false;
};

struct SnapshotNavigationState {
  int initialIndex = 0;
  int currentFileIndex = 0;
  int batchSize = 1;
  int skipStep = 1;
  int currentStep = 0;
};

struct SnapshotInputState {
  char fileFormat[255] = "output_%04d.dat";
  char folderPath[255] = "./example/";
  char filePath[512]   = "./example/output_0000.dat";
#ifdef HAVE_HDF5
  bool useHDF5 = false;
#endif
};

struct SnapshotCurrentState {
  int loadedFileIndex = -1;
  double loadedTime = 0.0;
  double loadedScaleFactor = 1.0;
  double loadedRedshift = 0.0;
  double loadedCosmicTime = 0.0;
  bool hasCosmicTime = false;
  bool useComovingCoordinates = false;
};

inline std::vector<FieldSpec> MakeDefaultSnapshotFormatTokens()
{
  std::vector<FieldSpec> tokens;
  tokens.reserve(12);

  auto push = [&](FieldKey key, DataType type, int count) {
    FieldSpec spec;
    spec.key = key;
    spec.type = type;
    spec.count = count;
    spec.sourceName = GetDefaultHDF5SourceName(key);
    tokens.push_back(std::move(spec));
  };

  push(FieldKey::Position,    DataType::Float, 3);
  push(FieldKey::Velocity,    DataType::Float, 3);
  push(FieldKey::Type,        DataType::Int32, 1);
  push(FieldKey::ID,          DataType::Int32, 1);
  push(FieldKey::Hsml,        DataType::Float, 1);
  push(FieldKey::Density,     DataType::Float, 1);
  push(FieldKey::Temperature, DataType::Float, 1);
  push(FieldKey::Dummy,       DataType::Float, 1);
  push(FieldKey::Value,       DataType::Float, 1);
  push(FieldKey::Value2,      DataType::Float, 1);
  push(FieldKey::Dummy,       DataType::Float, 4);
  push(FieldKey::Mass,        DataType::Float, 1);

  return tokens;
}

struct SnapshotFormatState {
  FileFormat readFormat = FileFormat::Auto;
  std::vector<FieldSpec> formatTokens = MakeDefaultSnapshotFormatTokens();
  std::vector<FieldSpec> formatTokensHdf5 = MakeDefaultSnapshotFormatTokens();
};

struct FileNavigationRuntimeState {
  SnapshotNavigationState navigation;
  SnapshotInputState input;
  SnapshotCurrentState current;
  int tempSkipStep = 1;
  FileNavigationRequestState request;
};

struct SnapshotPostprocessState {
  bool refreshTree = false;
  bool refreshCulling = false;
  bool refreshTopParticles = false;
  bool applyTrackingToCamera = false;
};

struct SettingsRenderEditDraft {
  ParticleLabelRenderState particleLabels;
  VelocityRenderState velocity;
  float diskOpacity = 1.0f;
  float ellipsoidOpacity = 1.0f;
  float isoContourOpacity = 1.0f;
  float crossGizmoSize = 0.05f;
};

struct SettingsActionRequestState {
  bool normalizeRequested = false;
  bool particleRenderDirtyRequested = false;
  bool velocityRenderDirtyRequested = false;
  bool unitConversionRebuildRequested = false;

  bool particleVisualDraftDirty = false;
  bool applyParticleVisualRequested = false;
  ParticleVisualConfig particleVisualDraft;

  bool renderDraftDirty = false;
  bool applyRenderRequested = false;
  SettingsRenderEditDraft renderDraft;

  bool unitsDraftDirty = false;
  bool applyUnitsRequested = false;
  UnitSystem unitsDraft;
};

struct SettingsRuntimeState {
  float minZoom = 0.1f;
  float maxZoom = 500.0f;

  ViewFilterConfig viewFilter;
  InputFilterConfig inputFilter;
  SnapshotFormatState snapshotFormat;
  NormalizationContext normalization;
  FileNavigationRuntimeState fileNavigation;
  CameraPlacementRequestState cameraPlacement;
  TrackingTargetState tracking;
  SettingsActionRequestState request;
};
