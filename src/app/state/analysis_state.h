#pragma once
#include <array>
#include <string>
#include <vector>

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
  struct SeedReport {
    int seedIndex = -1;
    float position[3] = {0.f, 0.f, 0.f};
    int stopReason = 0;
    int pointCount = 0;
    float length = 0.0f;
  };

  bool cpuUpdated = false;
  bool success = false;
  std::string message;
  int seedCount = 0;
  int lineCount = 0;
  std::array<int, 7> stopCounts{};
  std::vector<SeedReport> seedReports;
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

#ifdef VOLUME_RENDERING
#include "volume/adaptive_volume_tree.h"
#endif

struct RadialProfileResultState {
  bool computed = false;
  RadialProfileResult result;
};

struct Histogram2DResultState {
  bool computed = false;
  Histogram2DResult result;
};

#ifdef VOLUME_RENDERING
struct VolumeRenderingResultState {
  bool valid = false;
  bool cpuUpdated = false;
  bool success = false;
  std::string message;
  AdaptiveVolumeTree tree;
  AdaptiveVolumeTreeStats stats;
};
#endif

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

#ifdef VOLUME_RENDERING
  VolumeRenderingResultState volume;
#endif

#ifdef CLUMP_DATA_READ
  ClumpBatchResultState clumpBatch;
#endif

  ProjectionMovieResultState projectionMovie;
  ProjectionPreviewDerivedState projectionPreview;
  
  RadialProfileResultState radial;
  Histogram2DResultState hist2D;
};
