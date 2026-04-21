#pragma once
#include <string>
#include "core/tracking_vector.h"

struct ClumpFinderWindowState {
  bool open = false;

  int selectedVar = 0;

  float minPeakDensity = 0.0f;
  bool showLeaves = false;

  bool histogramAutoRange = true;
  float histogramRangeMin = 0.0f;
  float histogramRangeMax = 1.0f;

  bool histogramLogScaleX = false;
  bool histogramLogScaleY = false;

  int histogramBins = 50;
  bool histogramBinsAuto = false;
};

struct LoadedClumpWindowState {
  bool open = false;

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
  bool requestReload = false;
  bool requestUpdateEvolutionCache = false;
  bool requestFollowSelected = false;

  TrackingVector<bool> showEvolve;
};

struct ClumpChainWindowState {
  bool open = false;

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
  int clumpChainInitFileIndex = 0;
  int clumpChainNsnapshots = 1;
  int clumpChainDFileIndex = 1;
  char clumpChainFileName[512]="";
};
