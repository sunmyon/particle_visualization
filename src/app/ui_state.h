#pragma once
#include <vector>
#include "core/tracking_vector.h"
#include "data/particle_data.h"
#include "data/particle_mask_config.h"
#include "app/file_format_dialog_state.h"
#include "FindClumps/clump_window_state.h"

struct RadialProfileUIState {
  bool open = false;
};

struct Histogram2DUIState {
  bool open = false;
};

struct ImFont;
struct ProjectionMapUIState {
  bool open = false;
  bool useOriginalCoordinate = true;
  bool selectMode = false;

  bool fontWindowOpen = false;
  bool previewFontsInitialized = false;
  int currentFontIndex = 0;

  float xlen_input[3] = {2.0f, 2.0f, 1.0f};
  std::vector<ImFont*> previewFonts;
};

struct TopParticlesUIState {
  int queryID = -1;
  bool hasFound = false;
  ParticleData foundParticle{};

  int particleType = 3;
  int m = 10;

  std::deque<ParticleData> historyData;
  int historySel = -1;

  bool selectType[6] = {false, false, false, false, false, false};

  TrackingVector<ParticleData> filtered;

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

  bool histogramComputed = false;
  TrackingVector<float> histBins;
  TrackingVector<float> binCenters;

  float vmin = 0.0f;
  float vmax = 1.0f;
  float binSize = 1.0f;
};

struct HaloesUIState {
  bool open = false;
  char fname[255] = "";

  std::vector<uint8_t> selectedForStress;
  bool stressSelectionDirty = false;
  bool requestRecomputePositions = false;
  
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
  TrackingVector<float> histBins;
  TrackingVector<float> binCenters;
  float vmin = 0.0f;
  float vmax = 1.0f;
  float binSize = 1.0f;
};

struct MaskUIState {
  bool open = false;
  bool autoApply = true;
  uint64_t revision = 0;
};

struct ToolWindowUIState {
  RadialProfileUIState radialProfile;
  Histogram2DUIState histogram2D;
  ProjectionMapUIState projectionMap;
  TopParticlesUIState topParticles;
  HaloesUIState haloes;
  MaskUIState mask;
  FileFormatDialogState fileFormatDialog;
  ClumpFinderWindowState clumpFind;
  LoadedClumpWindowState clumpList;
  ClumpChainWindowState clumpChain;
};
