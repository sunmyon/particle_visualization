#pragma once

#include <cstdint>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "app/state/clump_window_state.h"
#include "app/state/file_format_dialog_state.h"
#include "app/state/plot_export_state.h"
#include "analysis/histogram2d.h"
#include "analysis/radial_profile.h"
#include "core/quantity.h"
#include <vector>
#include "data/simulation_element.h"
#include "data/particle_mask_config.h"
#include "projection/projection_map_params.h"
#include "projection/projection_map_ui_state.h"

struct QuantityState;

struct RadialProfileUIState {
  bool open = false;
  int selectedXAxis = 0;
  int selectedVarIdx = 0;
  RadialProfileParams draftParams;
  bool exportPlotPackage = true;
  uint64_t lastExportedVersion = 0;
  char exportFolder[512] = "";
  char lastExportStatus[512] = "";
};

struct RadialProfileRequestState {
  RadialProfileParams params;
  bool runRequested = false;
};

struct Histogram2DUIState {
  bool open = false;
  Histogram2DParams draftParams;
  bool exportPlotPackage = true;
  uint64_t lastExportedVersion = 0;
  char exportFolder[512] = "";
  char lastExportStatus[512] = "";
};

struct Histogram2DRequestState {
  Histogram2DParams params;
  bool runRequested = false;
};

struct ImFont;
struct ProjectionMapUIState {
  bool open = false;
  bool paramsInitialized = false;
  uint64_t observedToolRevision = 0;
  ProjectionMapParams draftParams;
  bool selectMode = false;
  bool dragInitialized = false;
  float dragLastX = 0.0f;
  float dragLastY = 0.0f;
  bool previewOpen = true;
  uint64_t observedPreviewVersion = 0;

  bool layoutEditorOpen = false;
  int selectedPanelIndex = 0;
  int selectedViewBlockIndex = 0;
  int selectedStarOverlayIndex = 0;
  int selectedVectorOverlayIndex = 0;

  bool fontWindowOpen = false;
  bool fontListRefreshRequested = true;
  int currentFontIndex = 0;
  int appliedFontIndex = -1;

  float xlen_input[3] = {2.0f, 2.0f, 1.0f};
  float xoffset_input[3] = {0.0f, 0.0f, 0.0f};
  std::vector<QuantityId> gasQuantityOptions;
  std::vector<std::string> availableFontPaths;
  std::vector<ImFont*> previewFonts;
};

struct ProjectionFontSelectionRequestState {
  bool applySelectedFontRequested = false;
};

struct TopParticlesUIState {
  bool open = true;
  int64_t queryID = -1;

  int particleType = 3;
  int m = 10;
  QuantityId sortQuantity = QuantityId::Mass;
  bool sortDescending = true;

  int historySel = -1;

  bool selectType[6] = {false, false, false, false, false, false};

  int selectedVar = 4;
  int bins = 50;

  bool histogramLogScaleX = true;
  bool histogramLogScaleY = true;
  bool autoRange = true;

  float range1_min = 0.0f;
  float range1_max = 1.0f;
  float range2_min = 0.0f;
  float range2_max = 1.0f;

  bool useCameraCenter = false;
  float cameraRadius = 10.0f;
  bool exportHistogramPackage = true;
  uint64_t lastExportedHistogramVersion = 0;
  char exportFolder[512] = "";
  char lastExportStatus[512] = "";
};

struct TopParticlesRequestState {
  bool queryParticleRequested = false;
  bool refreshHistoryRequested = false;
  bool clearHistoryRequested = false;
  bool refreshFilteredRequested = false;
  bool computeHistogramRequested = false;
  bool centerParticleRequested = false;
  bool followParticleRequested = false;
  bool disableFollowParticleRequested = false;

  int64_t queryParticleId = -1;
  int64_t centerParticleId = -1;
  size_t centerParticleIndex = static_cast<size_t>(-1);

  bool selectedTypes[6] = {false, false, false, false, false, false};

  int histogramSelectedVar = 4;
  int histogramBins = 50;
  bool histogramLogScaleX = true;
  bool histogramLogScaleY = true;
  bool histogramAutoRange = true;
  float histogramRange1Min = 0.0f;
  float histogramRange1Max = 1.0f;
  float histogramRange2Min = 0.0f;
  float histogramRange2Max = 1.0f;
  bool histogramUseCameraCenter = false;
  float histogramCameraRadius = 10.0f;

  int topCount = 10;
  QuantityId sortQuantity = QuantityId::Mass;
  bool sortDescending = true;
};

struct TopParticlesResultState {
  bool hasFound = false;
  bool queryFailed = false;
  SimulationElement foundParticle{};
  int64_t foundParticleId = -1;

  std::deque<SimulationElement> historyData;
  std::deque<int64_t> historyIds;
  std::vector<SimulationElement> filtered;
  std::vector<int64_t> filteredIds;
  std::vector<size_t> filteredIndices;
  std::vector<float> filteredSortValues;
  size_t filteredCandidateCount = 0;
  QuantityId filteredSortQuantity = QuantityId::Mass;
  bool filteredSortDescending = true;

  bool histogramComputed = false;
  uint64_t histogramVersion = 0;
  std::vector<float> histBins;
  std::vector<float> binCenters;

  float vmin = 0.0f;
  float vmax = 1.0f;
  float binSize = 1.0f;
};

struct TopParticlesViewContext {
  const QuantityState* quantity = nullptr;
  PlotBatchExportViewContext exportContext;
};

struct HaloRowView {
  int sourceIndex = -1;
  int groupLen = 0;
  float groupMass = 0.0f;
  float groupMassType[6] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
  float groupPos[3] = {0.f, 0.f, 0.f};
  float groupVel[3] = {0.f, 0.f, 0.f};
  float groupMetallicity[2] = {0.f, 0.f};
};

struct HaloesUIState {
  bool open = false;
  char fname[255] = "";

  bool loaded = false;
  bool idsLoaded = false;
  std::vector<HaloRowView> rows;
  std::vector<uint8_t> selectedForStress;

  int m = 50;

  bool recomputeUseMassWeight = true;
  bool recomputeUseOriginalPos = true;
  int recomputeMinParticles = 20;

  int selectedVar = 0;
  int bins = 20;

  bool histogramLogScaleX = true;
  bool histogramLogScaleY = true;
  bool autoRange = true;

  float range1_min = 0.0f;
  float range1_max = 1.0f;
  float range2_min = 0.0f;
  float range2_max = 1.0f;

  bool histogramComputed = false;
  uint64_t histogramVersion = 0;
  std::vector<float> histBins;
  std::vector<float> binCenters;
  float vmin = 0.0f;
  float vmax = 1.0f;
  float binSize = 1.0f;
  bool exportHistogramPackage = true;
  uint64_t lastExportedHistogramVersion = 0;
  char exportFolder[512] = "";
  char lastExportStatus[512] = "";
};

struct HaloesRequestState {
  bool loadWithoutIdsRequested = false;
  bool loadWithIdsRequested = false;
  bool clearIdsRequested = false;
  bool resetSelectionRequested = false;
  bool recomputePositionsRequested = false;
  bool stressSelectionChanged = false;
  bool focusHaloRequested = false;
  bool computeHistogramRequested = false;

  char filename[255] = "";
  bool recomputeUseMassWeight = true;
  bool recomputeUseOriginalPos = true;
  int recomputeMinParticles = 20;
  std::vector<uint8_t> selectedForStress;

  int focusHaloIndex = -1;

  int histogramSelectedVar = 0;
  int histogramBins = 20;
  bool histogramLogScaleX = true;
  bool histogramLogScaleY = true;
  bool histogramAutoRange = true;
  float histogramRange1Min = 0.0f;
  float histogramRange1Max = 1.0f;
  float histogramRange2Min = 0.0f;
  float histogramRange2Max = 1.0f;
};

struct MaskUIState {
  bool open = false;
  bool autoApply = true;
  uint64_t revision = 0;
};

struct MaskRequestState {
  bool applyRequested = false;
};

struct ToolWindowUIState {
  RadialProfileUIState radialProfile;
  RadialProfileRequestState radialProfileRequest;
  Histogram2DUIState histogram2D;
  Histogram2DRequestState histogram2DRequest;
  ProjectionMapUIState projectionMap;
  ProjectionFontSelectionRequestState projectionFontSelectionRequest;
  TopParticlesUIState topParticles;
  TopParticlesRequestState topParticlesRequest;
  TopParticlesResultState topParticlesResult;
  HaloesUIState haloes;
  HaloesRequestState haloesRequest;
  MaskUIState mask;
  MaskRequestState maskRequest;
  FileFormatDialogState fileFormatDialog;
  ClumpFinderWindowState clumpFind;
  LoadedClumpWindowState clumpList;
  ClumpChainWindowState clumpChain;
};
