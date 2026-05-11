#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum class ClumpFinderPlotQuantity : int {
  MassMsun = 0,
  Density = 1,
  Temperature = 2,
  PeakValue = 3,
  Count = 4,
  StellarMassMsun = 5,
  StellarCount = 6,
  Depth = 7
};

static constexpr int kNumClumpFinderPlotQuantities = 8;

struct ClumpFinderRowView {
  int sourceIndex = -1;
  int count = 0;
  double mass = 0.0;
  double stellarMass = 0.0;
  int stellarCount = 0;
  float density = 0.0f;
  float temperature = 0.0f;
  float pos[3] = {0.f, 0.f, 0.f};
  float vpeak = 0.0f;
  bool isLeaf = false;
  bool isTrunk = false;
  int depth = 0;
  int parentSourceIndex = -1;
  int childCount = 0;
};

struct ClumpFinderWindowState {
  bool open = false;

  int selectedVar = 0;

  float densityThreshold = 10.0f;
  int minParticles = 30;
  float minDensityContrastRatio = 10.0f;
  bool useHsml = true;
  float linkingLength = 0.01f;
  float linkingLengthOverCellSize = 2.0f;

  bool requestRunFOF = false;
  bool requestRunDendrogram = false;
  bool requestSortByMass = false;
  bool requestSortByHierarchy = false;
  bool requestApplyHullSelection = false;
  bool requestComputeHistogram = false;
  bool requestComputeScatter = false;
  bool requestFocusRow = false;
  int focusRowIndex = -1;
  bool clumpsComputed = false;

  float minPeakDensity = 0.0f;
  bool showLeaves = false;

#ifdef CLUMP_DATA_READ
  char outputFileName[255] = "clumpList.hdf5";
  bool requestOutputHdf5 = false;
#endif

  int histogramSelectedVar = static_cast<int>(ClumpFinderPlotQuantity::MassMsun);
  float histogramRangeMin = 1.0f;
  float histogramRangeMax = 1.0e8f;
  float histogramCountMin = 0.0f;
  float histogramCountMax = 100.0f;

  bool histogramLogScaleX = true;
  bool histogramLogScaleY = false;

  int histogramBins = 50;

  bool histogramComputed = false;
  uint64_t histogramVersion = 0;
  std::vector<float> histogramValues;

  int scatterXVar = static_cast<int>(ClumpFinderPlotQuantity::MassMsun);
  int scatterYVar = static_cast<int>(ClumpFinderPlotQuantity::Temperature);
  int plotYSelection = 0;
  bool scatterLogScaleX = true;
  bool scatterLogScaleY = true;
  float scatterRangeXMin = 1.0f;
  float scatterRangeXMax = 1.0e8f;
  float scatterRangeYMin = 1.0f;
  float scatterRangeYMax = 1.0e8f;
  bool scatterComputed = false;
  uint64_t scatterVersion = 0;
  std::vector<float> scatterXValues;
  std::vector<float> scatterYValues;

  std::vector<ClumpFinderRowView> rows;
  std::vector<bool> showHull;
  bool exportHistogramPackage = true;
  uint64_t lastExportedHistogramVersion = 0;
  char exportFolder[512] = "";
  char lastExportStatus[512] = "";
};

struct LoadedClumpEvolutionCacheView {
  std::vector<float> timeFloats;
  std::vector<float> valueFloats;
  int index = -1;
  int clumpID = -1;
};

struct LoadedClumpRowView {
  int sourceIndex = -1;
  int clumpID = -1;
  int count = 0;
  float mass = 0.0f;
  float density = 0.0f;
  float pos[3] = {0.f, 0.f, 0.f};
  int stellarCount = 0;
  float stellarMass = 0.0f;
  int stellarID = -1;
};

struct LoadedClumpWindowState {
  bool open = false;

  char clumpListPath[255] = "";
  bool requestUseInputPath = false;
  int selectedClumpID = -1;

  int finalFileIndex = 1000;
  int dsnapshot = 10;

  bool autoRangeX = true;
  float tMinInput = 0.0f;
  float tMaxInput = 1.0f;

  int selectedEvolutionVar = 0;

  bool useLogScaleY = true;
  bool autoRangeY = true;
  float valMinInput = 1.0f;
  float valMaxInput = 1.0e10f;

  bool showEvolutionPlot = false;
  uint64_t evolutionVersion = 0;
  bool exportEvolutionPackage = true;
  uint64_t lastExportedEvolutionVersion = 0;
  char exportFolder[512] = "";
  char lastExportStatus[512] = "";
  bool requestReload = false;
  bool requestUpdateEvolutionCache = false;
  bool requestFollowSelected = false;
  bool requestFocusSelected = false;
  int focusClumpIndex = -1;

  std::vector<bool> showEvolve;
  std::vector<LoadedClumpRowView> rows;
  std::vector<LoadedClumpEvolutionCacheView> evolutionCache;
};

struct ClumpChainSnapshotView {
  float time = 0.0f;
  float pos[3] = {0.f, 0.f, 0.f};
  float density = 0.0f;
  float temperature = 0.0f;
  float mass = 0.0f;
  float stellarMass = 0.0f;
};

struct ClumpChainSeriesView {
  int firstSnapshot = -1;
  int lastSnapshot = -1;
  int globalId = -1;
  int stellarId = -1;
  int nstar = 0;
  float mstar = 0.0f;
  float mstarMaximum = 0.0f;
  float massMaximum = 0.0f;
  float temperature_d = 0.0f;
  bool plot = false;
  std::vector<ClumpChainSnapshotView> snapshots;
};

struct ClumpChainWindowState {
  bool open = false;
  bool computed = false;
  std::vector<ClumpChainSeriesView> series;

  int selectedChainIndex = -1;
  int currentSnapshotIndex = 0;

  int selectedVar = 0;
  bool useLogScaleY = true;
  bool autoScale = true;
  float xmin = 0.0f;
  float xmax = 1.0f;
  float ymin = 1.0f;
  float ymax = 1.0e10f;

  float mapLen = 1.0f;
  float mapValMin = 1.0e2f;
  float mapValMax = 1.0e8f;
  int mapNpixel = 400;
  int mapNslices = 100;
  char mapOutputDir[512]="";
  
  int selectedProjectionVar = 0;

  bool requestBuild = false;
  bool requestLoadSelected = false;
  bool requestPrev = false;
  bool requestNext = false;
  bool requestFixedView = false;
  bool requestMakeProjectionMaps = false;

  bool flagFileLoaded = false;
  bool navigationLoadPending = false;
  int navigationPendingStep = -1;
  float navigationPendingCenter[3] = {0.f, 0.f, 0.f};

  bool projectionBatchRunning = false;
  int projectionBatchCursor = 0;
  int projectionBatchChainIndex = -1;
  uint64_t evolutionVersion = 0;
  bool exportEvolutionPackage = true;
  uint64_t lastExportedEvolutionVersion = 0;
  char exportFolder[512] = "";
  char lastExportStatus[512] = "";

  int clumpChainInitFileIndex = 0;
  int clumpChainNsnapshots = 1;
  int clumpChainDFileIndex = 1;
  char clumpChainFileName[512]="";
};
