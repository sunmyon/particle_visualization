#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include "analysis/histogram2d.h"
#include "analysis/radial_profile.h"
#include <vector>

struct AnalysisBatchSnapshotSpec {
  std::string folderPath;
  std::string fileFormat;
  int initialIndex = 0;
  int currentStep = 0;
  int skipStep = 1;
  int batchSize = 1;
  bool useHDF5 = false;
};

struct AnalysisBatchCameraSpec {
  glm::vec3 position{0.0f, 0.0f, 5.0f};
  glm::vec3 target{0.0f, 0.0f, 0.0f};
};

struct AnalysisPlotExportSpec {
  std::filesystem::path directory;
  std::string stem;
  int imageWidth = 900;
  int imageHeight = 600;
  AnalysisBatchSnapshotSpec snapshot;
  AnalysisBatchCameraSpec camera;
};

struct AnalysisPlotExportResult {
  bool ok = false;
  std::filesystem::path jobJsonPath;
  std::filesystem::path dataPath;
  std::filesystem::path imagePath;
  std::string error;
};

struct BarHistogramPlotExportParams {
  std::string kind;
  std::string title = "Histogram";
  std::string xLabel = "x";
  std::string yLabel = "Count";
  bool logX = false;
  bool logY = false;
  float xMin = 0.0f;
  float xMax = 1.0f;
  float yMin = 0.0f;
  float yMax = 1.0f;
  float binSize = 1.0f;
};

struct LineSeriesPlotExportParams {
  std::string kind;
  std::string title = "Time Evolution";
  std::string xLabel = "Time";
  std::string yLabel = "Value";
  bool logY = false;
  float xMin = 0.0f;
  float xMax = 1.0f;
  float yMin = 0.0f;
  float yMax = 1.0f;
};

struct PlotLineSeries {
  std::string label;
  std::vector<float> x;
  std::vector<float> y;
};

bool WriteRadialProfileData(const std::filesystem::path& path,
                            const RadialProfileResult& result);

bool WriteHistogram2DData(const std::filesystem::path& path,
                          const Histogram2DResult& result);

bool WriteRadialProfilePlotPng(const std::filesystem::path& path,
                               const RadialProfileParams& params,
                               const RadialProfileResult& result,
                               int width,
                               int height);

bool WriteHistogram2DPlotPng(const std::filesystem::path& path,
                             const Histogram2DParams& params,
                             const Histogram2DResult& result,
                             int width,
                             int height);

AnalysisPlotExportResult ExportRadialProfilePlotPackage(
  const AnalysisPlotExportSpec& spec,
  const RadialProfileParams& params,
  const RadialProfileResult& result);

AnalysisPlotExportResult ExportHistogram2DPlotPackage(
  const AnalysisPlotExportSpec& spec,
  const Histogram2DParams& params,
  const Histogram2DResult& result);

AnalysisPlotExportResult ExportBarHistogramPlotPackage(
  const AnalysisPlotExportSpec& spec,
  const BarHistogramPlotExportParams& params,
  const std::vector<float>& centers,
  const std::vector<float>& values);

AnalysisPlotExportResult ExportLineSeriesPlotPackage(
  const AnalysisPlotExportSpec& spec,
  const LineSeriesPlotExportParams& params,
  const std::vector<PlotLineSeries>& series);
