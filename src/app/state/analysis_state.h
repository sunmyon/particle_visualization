#pragma once
#include "convex_hull_state.h"
#include "image/rgb_image.h"
#include "render/scene_objects.h"

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
#include "analysis/isosurface/iso_contour_geometry.h"
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

#include "analysis/radial_profile.h"
#include "analysis/histogram2d.h"

struct RadialProfileResultState {
  bool computed = false;
  RadialProfileResult result;
};

struct Histogram2DResultState {
  bool computed = false;
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
  
  RadialProfileResultState radial;
  Histogram2DResultState hist2D;
};
