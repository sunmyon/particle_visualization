#pragma once

#include <vector>
#include "data/clump_data.h"

struct ClumpFinderUIState {
  int selectedVar = 0;

  float minPeakDensity = 0.0f;
  bool flagShowLeaves = false;

  bool histogramAutoRange = true;
  float histogramRangeMin = 0.0f;
  float histogramRangeMax = 1.0f;

  bool histogramLogScaleX = false;
  bool histogramLogScaleY = false;

  int histogramBins = 50;
  bool histogramBinsAuto = false;

  std::vector<float> massHistogramValues;
};

struct ClumpEvolutionCache {
  std::vector<float> timeFloats;
  std::vector<float> valueFloats;
  int index = -1;
};

struct LoadedClumpToolState {
  bool flagReadClumpFile = false;

  int selectedClumpID = -1;
  std::vector<bool> showEvolve;

  int finalFileIndex = 1000;
  int dsnapshot = 10;

  bool flagAutoRangeX = true;
  float t_min_input = 0.0f;
  float t_max_input = 1.0f;

  int selectedEvolutionVar = 0;

  bool useLogScale = true;
  bool flagAutoRangeY = true;
  float val_min_input = 1.0f;
  float val_max_input = 1.0e10f;

  bool flagShowClumpEvolution = false;
  bool flagUpdateClumpCache = false;

  float t_min = 0.0f;
  float t_max = 1.0f;
  float val_min = 1.0f;
  float val_max = 1.0e10f;

  std::vector<ClumpEvolutionCache> evolutionCache;
};

struct ClumpChainToolState {
  bool flagClumpChainComputed = false;

  int clumpChainInitFileIndex = 0;
  int clumpChainNsnapshots = 0;
  int clumpChainDFileIndex = 1;
  char clumpChainFileName[256] = "";

  std::vector<ClumpChainProps> clumpChainProps;
  std::vector<std::vector<ClumpSnapshot*>> clumpChain;

  std::vector<bool> plotClumps;
  int selectedChainIndex = -1;

  int currentChainSnapshot = 0;
  bool flagButtonPushed = false;
  bool flagFileLoaded = false;

  int selectedVar = 0;
  bool useLogScale = true;
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
  char outputDir[255] = "";

  int selectedProjectionVar = 0;
};
