#pragma once
#include "convex_hull_state.h"
#include "object.h"

struct DiskAnalysisResultState {
  bool valid = false;
  bool cpuUpdated = false;
  int targetParticleId = -1;
  float radius = 0.0f;
  DiskObject disk;
};

struct DiskAnalysisBatchResultState {
  bool running = false;
  bool completed = false;
  bool success = false;
  int processedRows = 0;
  std::string lastInputFile;
  std::string lastOutputFile;
};

struct EllipsoidAnalysisResultState {
  bool valid = false;
  bool cpuUpdated = false;
  EllipsoidObject ellipsoid;
};

struct EllipsoidAnalysisBatchResultState {
  bool completed = false;
  bool success = false;
  int processedRows = 0;
  std::string lastInputFile;
  std::string lastOutputFile;
};

struct StreamlinePreviewResultState {
  bool valid = false;
  bool cpuUpdated = false;
  CubeObject cube;
};

struct StreamlineBuildResultState {
  bool cpuUpdated = false;
  TrackingVector<LineObject> lines;
};

struct AnalysisDerivedState {
  ConvexHullRuntimeState convexHulls;
  
  DiskAnalysisResultState disk;
  DiskAnalysisBatchResultState diskBatch;

  EllipsoidAnalysisResultState ellipsoid;
  EllipsoidAnalysisBatchResultState ellipsoidBatch;

  StreamlinePreviewResultState streamlinePreview;
  StreamlineBuildResultState streamlineBuild;
};
