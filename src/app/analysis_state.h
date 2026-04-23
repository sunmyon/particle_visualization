#pragma once
#include "convex_hull_state.h"
#include "image/rgb_image.h"
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

#ifdef ISO_CONTOUR
struct IsoContourGeometryState {
  TrackingVector<float> verts;
  TrackingVector<unsigned> inds;

  void clear() {
    verts.clear();
    inds.clear();
  }
};
#endif

#ifdef CLUMP_DATA_READ
struct ClumpBatchResultState {
  bool completed = false;
  int processedSnapshots = 0;
  char outputPath[512] = "";
  char errorMessage[512] = "";
};
#endif

struct ProjectionMovieResultState {
  bool completed = false;
  bool success = false;
  int processedSnapshots = 0;
  char outputMoviePath[512] = "";
  char errorMessage[512] = "";
};

#include "compute_radial_profile.h"
#include "compute_2D_histogram.h"

struct RadialProfileRuntimeState {
  bool computed = false;
  int selectedXAxis = 0;
  int selectedVarIdx = 0;
  
  RadialProfileParams params;
  RadialProfileResult result;
};

struct Histogram2DRuntimeState {
  bool computed = false;

  Histogram2DParams params;
  Histogram2DResult result;
};

struct ProjectionPreviewDerivedState {
  bool computed = false;
  
  RgbImage image;
  bool valid = false;
  uint64_t version = 0;
};

struct AnalysisDerivedState {
  ConvexHullRuntimeState convexHulls;
  
  DiskAnalysisResultState disk;
  DiskAnalysisBatchResultState diskBatch;

  EllipsoidAnalysisResultState ellipsoid;
  EllipsoidAnalysisBatchResultState ellipsoidBatch;
  
#ifdef STREAM_LINE  
  StreamlinePreviewResultState streamlinePreview;
  StreamlineBuildResultState streamlineBuild;
#endif
  
#ifdef ISO_CONTOUR
  IsoContourGeometryState isoContour;
#endif

#ifdef CLUMP_DATA_READ
  ClumpBatchResultState clumpBatch;
#endif

  ProjectionMovieResultState projectionMovie;
  ProjectionPreviewDerivedState projectionPreview;
  
  RadialProfileRuntimeState radial;
  Histogram2DRuntimeState hist2D;
};
