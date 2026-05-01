#include "analysis/profile_histogram_export.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>

#include <nlohmann/json.hpp>

#include "core/tracking_vector.h"
#include "image/image_io.h"
#include "render/colormap_defs.h"

using json = nlohmann::json;

namespace {

struct Rgb {
  unsigned char r = 0;
  unsigned char g = 0;
  unsigned char b = 0;
};

void SetPixel(TrackingVector<unsigned char>& rgb,
              int width,
              int height,
              int x,
              int y,
              Rgb c)
{
  if (x < 0 || y < 0 || x >= width || y >= height) {
    return;
  }
  const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(width) +
                      static_cast<size_t>(x)) * 3;
  rgb[idx + 0] = c.r;
  rgb[idx + 1] = c.g;
  rgb[idx + 2] = c.b;
}

void FillRect(TrackingVector<unsigned char>& rgb,
              int width,
              int height,
              int x0,
              int y0,
              int x1,
              int y1,
              Rgb c)
{
  x0 = std::clamp(x0, 0, width);
  x1 = std::clamp(x1, 0, width);
  y0 = std::clamp(y0, 0, height);
  y1 = std::clamp(y1, 0, height);
  if (x1 < x0) std::swap(x0, x1);
  if (y1 < y0) std::swap(y0, y1);
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      SetPixel(rgb, width, height, x, y, c);
    }
  }
}

void DrawLine(TrackingVector<unsigned char>& rgb,
              int width,
              int height,
              int x0,
              int y0,
              int x1,
              int y1,
              Rgb c)
{
  const int dx = std::abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    SetPixel(rgb, width, height, x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

bool TransformValue(float value, bool logScale, float& out)
{
  if (!std::isfinite(value)) return false;
  if (!logScale) {
    out = value;
    return true;
  }
  if (value <= 0.0f) return false;
  out = std::log10(value);
  return true;
}

bool MakeAxisRange(float minValue,
                   float maxValue,
                   bool logScale,
                   float& outMin,
                   float& outMax)
{
  if (!TransformValue(minValue, logScale, outMin)) return false;
  if (!TransformValue(maxValue, logScale, outMax)) return false;
  if (!(outMax > outMin)) return false;
  return true;
}

int MapX(float value, float minValue, float maxValue, int x0, int x1)
{
  const float t = (value - minValue) / (maxValue - minValue);
  return x0 + static_cast<int>(std::lround(t * static_cast<float>(x1 - x0)));
}

int MapY(float value, float minValue, float maxValue, int y0, int y1)
{
  const float t = (value - minValue) / (maxValue - minValue);
  return y1 - static_cast<int>(std::lround(t * static_cast<float>(y1 - y0)));
}

Rgb SampleColormap(float t)
{
  const ColormapDef* maps = AvailableColormaps();
  const int nMaps = AvailableColormapCount();
  const ColormapDef* map = nMaps > 1 ? &maps[1] : (nMaps > 0 ? &maps[0] : nullptr);
  if (!map || !map->data || map->count <= 0) {
    const unsigned char v = static_cast<unsigned char>(std::clamp(t, 0.0f, 1.0f) * 255.0f);
    return {v, v, v};
  }
  t = std::clamp(t, 0.0f, 1.0f);
  const float p = t * static_cast<float>(map->count - 1);
  const int i0 = std::clamp(static_cast<int>(std::floor(p)), 0, map->count - 1);
  const int i1 = std::clamp(i0 + 1, 0, map->count - 1);
  const float f = p - static_cast<float>(i0);
  const float* c0 = &map->data[i0 * 3];
  const float* c1 = &map->data[i1 * 3];
  auto lerp = [&](int k) {
    return static_cast<unsigned char>(
      std::clamp(c0[k] * (1.0f - f) + c1[k] * f, 0.0f, 1.0f) * 255.0f);
  };
  return {lerp(0), lerp(1), lerp(2)};
}

const char* XModeToJsonName(XAxisMode mode)
{
  switch (mode) {
    case XAxisMode::Radius: return "radius";
    case XAxisMode::PosX: return "x";
    case XAxisMode::PosY: return "y";
    case XAxisMode::PosZ: return "z";
    case XAxisMode::EnclosedMass: return "enclosed_mass";
  }
  return "radius";
}

json SnapshotToJson(const AnalysisBatchSnapshotSpec& snapshot)
{
  json out;
  out["folderPath"] = snapshot.folderPath;
  out["fileFormat"] = snapshot.fileFormat;
  out["initialIndex"] = snapshot.initialIndex;
  out["currentStep"] = snapshot.currentStep;
  out["skipStep"] = snapshot.skipStep;
  out["batchSize"] = snapshot.batchSize;
  out["useHDF5"] = snapshot.useHDF5;
  return out;
}

json CameraToJson(const AnalysisBatchCameraSpec& camera)
{
  return json{
    {"position", {camera.position.x, camera.position.y, camera.position.z}},
    {"target", {camera.target.x, camera.target.y, camera.target.z}}
  };
}

json RadialParamsToJson(const RadialProfileParams& params,
                        const std::filesystem::path& dataPath,
                        const std::filesystem::path& imagePath)
{
  json out;
  out["useOriginal"] = params.useOriginal;
  out["bins"] = params.bins;
  out["xmode"] = XModeToJsonName(params.xmode);
  out["isMDot"] = params.isMDot;
  out["quantity"] = QuantityLabel(params.var1);
  out["autoRange"] = params.autorange;
  out["absolute"] = params.flagAbsolute;
  out["logX"] = params.plotXAxisLog;
  out["logY"] = params.plotYAxisLog;
  out["xmin"] = params.xmin;
  out["xmax"] = params.xmax;
  out["ymin"] = params.ymin;
  out["ymax"] = params.ymax;
  out["rmax"] = params.rmax;
  out["outputFile"] = dataPath.string();
  out["outputImage"] = imagePath.string();
  return out;
}

json HistogramParamsToJson(const Histogram2DParams& params,
                           const std::filesystem::path& dataPath,
                           const std::filesystem::path& imagePath)
{
  json out;
  out["var1"] = QuantityLabel(params.var1);
  out["var2"] = QuantityLabel(params.var2);
  out["bins1"] = params.bins1;
  out["bins2"] = params.bins2;
  out["autoRange"] = params.autoRange;
  out["range1Min"] = params.range1_min;
  out["range1Max"] = params.range1_max;
  out["range2Min"] = params.range2_min;
  out["range2Max"] = params.range2_max;
  out["logX"] = params.logScaleX;
  out["logY"] = params.logScaleY;
  out["logColor"] = params.logScaleColor;
  out["useCameraCenter"] = params.useCameraCenter;
  out["cameraRadius"] = params.cameraRadius;
#ifdef USE_CONVEX_HULL
  out["useConvexHull"] = params.useConvexHull;
#endif
  out["outputFile"] = dataPath.string();
  out["outputImage"] = imagePath.string();
  return out;
}

bool WriteJsonFile(const std::filesystem::path& path, const json& value)
{
  std::ofstream out(path);
  if (!out) return false;
  out << value.dump(2) << '\n';
  return true;
}

bool WriteBarHistogramData(const std::filesystem::path& path,
                           const TrackingVector<float>& centers,
                           const TrackingVector<float>& values)
{
  std::ofstream out(path);
  if (!out) return false;
  out << "# x_center value\n";
  const size_t n = std::min(centers.size(), values.size());
  for (size_t i = 0; i < n; ++i) {
    out << centers[i] << ' ' << values[i] << '\n';
  }
  return true;
}

bool WriteLineSeriesData(const std::filesystem::path& path,
                         const std::vector<PlotLineSeries>& series)
{
  std::ofstream out(path);
  if (!out) return false;
  out << "# series_index x y\n";
  for (size_t s = 0; s < series.size(); ++s) {
    const size_t n = std::min(series[s].x.size(), series[s].y.size());
    for (size_t i = 0; i < n; ++i) {
      out << s << ' ' << series[s].x[i] << ' ' << series[s].y[i] << '\n';
    }
  }
  return true;
}

bool WriteBarHistogramPlotPng(const std::filesystem::path& path,
                              const BarHistogramPlotExportParams& params,
                              const TrackingVector<float>& centers,
                              const TrackingVector<float>& values,
                              int width,
                              int height)
{
  if (width <= 64 || height <= 64 || centers.empty() || values.empty()) return false;

  TrackingVector<unsigned char> rgb(static_cast<size_t>(width) *
                                    static_cast<size_t>(height) * 3,
                                    255);
  const int x0 = 72;
  const int x1 = width - 28;
  const int y0 = 28;
  const int y1 = height - 58;
  const Rgb black{25, 25, 25};
  const Rgb grid{225, 225, 225};
  const Rgb bar{70, 120, 210};

  for (int i = 0; i <= 5; ++i) {
    const int x = x0 + (x1 - x0) * i / 5;
    const int y = y0 + (y1 - y0) * i / 5;
    DrawLine(rgb, width, height, x, y0, x, y1, grid);
    DrawLine(rgb, width, height, x0, y, x1, y, grid);
  }
  DrawLine(rgb, width, height, x0, y1, x1, y1, black);
  DrawLine(rgb, width, height, x0, y0, x0, y1, black);

  float xmin = 0.0f, xmax = 1.0f, ymin = 0.0f, ymax = 1.0f;
  if (!MakeAxisRange(params.xMin, params.xMax, false, xmin, xmax)) return false;
  if (!MakeAxisRange(params.yMin, params.yMax, params.logY, ymin, ymax)) return false;

  const float binWidth = params.binSize > 0.0f ? params.binSize : (params.xMax - params.xMin) / values.size();
  const size_t n = std::min(centers.size(), values.size());
  for (size_t i = 0; i < n; ++i) {
    float yv = 0.0f;
    if (!TransformValue(values[i], params.logY, yv)) continue;
    if (yv < ymin || yv > ymax) continue;

    const float left = centers[i] - 0.5f * binWidth;
    const float right = centers[i] + 0.5f * binWidth;
    const int px0 = MapX(left, xmin, xmax, x0, x1);
    const int px1 = MapX(right, xmin, xmax, x0, x1);
    const int py = MapY(yv, ymin, ymax, y0, y1);
    FillRect(rgb,
             width,
             height,
             std::min(px0, px1),
             py,
             std::max(px0, px1) + 1,
             y1,
             bar);
  }

  return WritePngRgb(path.string().c_str(), width, height, rgb);
}

bool WriteLineSeriesPlotPng(const std::filesystem::path& path,
                            const LineSeriesPlotExportParams& params,
                            const std::vector<PlotLineSeries>& series,
                            int width,
                            int height)
{
  if (width <= 64 || height <= 64 || series.empty()) return false;

  TrackingVector<unsigned char> rgb(static_cast<size_t>(width) *
                                    static_cast<size_t>(height) * 3,
                                    255);
  const int x0 = 72;
  const int x1 = width - 28;
  const int y0 = 28;
  const int y1 = height - 58;
  const Rgb black{25, 25, 25};
  const Rgb grid{225, 225, 225};
  const Rgb colors[] = {
    {45, 110, 210}, {210, 85, 60}, {60, 155, 95}, {155, 80, 190},
    {230, 160, 40}, {60, 170, 190}
  };

  for (int i = 0; i <= 5; ++i) {
    const int x = x0 + (x1 - x0) * i / 5;
    const int y = y0 + (y1 - y0) * i / 5;
    DrawLine(rgb, width, height, x, y0, x, y1, grid);
    DrawLine(rgb, width, height, x0, y, x1, y, grid);
  }
  DrawLine(rgb, width, height, x0, y1, x1, y1, black);
  DrawLine(rgb, width, height, x0, y0, x0, y1, black);

  float xmin = 0.0f, xmax = 1.0f, ymin = 0.0f, ymax = 1.0f;
  if (!MakeAxisRange(params.xMin, params.xMax, false, xmin, xmax)) return false;
  if (!MakeAxisRange(params.yMin, params.yMax, params.logY, ymin, ymax)) return false;

  for (size_t s = 0; s < series.size(); ++s) {
    const Rgb color = colors[s % (sizeof(colors) / sizeof(colors[0]))];
    bool havePrev = false;
    int prevX = 0;
    int prevY = 0;
    const size_t n = std::min(series[s].x.size(), series[s].y.size());
    for (size_t i = 0; i < n; ++i) {
      float tx = series[s].x[i];
      float ty = 0.0f;
      if (!TransformValue(series[s].y[i], params.logY, ty) ||
          tx < xmin || tx > xmax || ty < ymin || ty > ymax) {
        havePrev = false;
        continue;
      }
      const int px = MapX(tx, xmin, xmax, x0, x1);
      const int py = MapY(ty, ymin, ymax, y0, y1);
      if (havePrev) {
        DrawLine(rgb, width, height, prevX, prevY, px, py, color);
      }
      FillRect(rgb, width, height, px - 1, py - 1, px + 2, py + 2, color);
      prevX = px;
      prevY = py;
      havePrev = true;
    }
  }

  return WritePngRgb(path.string().c_str(), width, height, rgb);
}

json BarHistogramParamsToJson(const BarHistogramPlotExportParams& params,
                              const std::filesystem::path& dataPath,
                              const std::filesystem::path& imagePath)
{
  json out;
  out["kind"] = params.kind;
  out["title"] = params.title;
  out["xLabel"] = params.xLabel;
  out["yLabel"] = params.yLabel;
  out["logX"] = params.logX;
  out["logY"] = params.logY;
  out["xMin"] = params.xMin;
  out["xMax"] = params.xMax;
  out["yMin"] = params.yMin;
  out["yMax"] = params.yMax;
  out["binSize"] = params.binSize;
  out["outputFile"] = dataPath.string();
  out["outputImage"] = imagePath.string();
  return out;
}

json LineSeriesParamsToJson(const LineSeriesPlotExportParams& params,
                            const std::vector<PlotLineSeries>& series,
                            const std::filesystem::path& dataPath,
                            const std::filesystem::path& imagePath)
{
  json out;
  out["kind"] = params.kind;
  out["title"] = params.title;
  out["xLabel"] = params.xLabel;
  out["yLabel"] = params.yLabel;
  out["logY"] = params.logY;
  out["xMin"] = params.xMin;
  out["xMax"] = params.xMax;
  out["yMin"] = params.yMin;
  out["yMax"] = params.yMax;
  out["outputFile"] = dataPath.string();
  out["outputImage"] = imagePath.string();
  out["seriesLabels"] = json::array();
  for (const auto& item : series) {
    out["seriesLabels"].push_back(item.label);
  }
  return out;
}

AnalysisPlotExportResult MakeExportError(std::string message)
{
  AnalysisPlotExportResult out;
  out.error = std::move(message);
  return out;
}

bool EnsureDirectory(const std::filesystem::path& dir, std::string& error)
{
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    error = ec.message();
    return false;
  }
  return true;
}

} // namespace

bool WriteRadialProfileData(const std::filesystem::path& path,
                            const RadialProfileResult& result)
{
  std::ofstream out(path);
  if (!out) return false;
  out << "# x y\n";
  const size_t n = std::min(result.x.size(), result.y.size());
  for (size_t i = 0; i < n; ++i) {
    out << result.x[i] << ' ' << result.y[i] << '\n';
  }
  return true;
}

bool WriteHistogram2DData(const std::filesystem::path& path,
                          const Histogram2DResult& result)
{
  std::ofstream out(path);
  if (!out) return false;
  out << "# x_center y_center value\n";
  for (size_t i = 0; i < result.centers1.size(); ++i) {
    for (size_t j = 0; j < result.centers2.size(); ++j) {
      float value = 0.0f;
      if (i < result.values.size() && j < result.values[i].size()) {
        value = result.values[i][j];
      }
      out << result.centers1[i] << ' ' << result.centers2[j] << ' ' << value << '\n';
    }
  }
  return true;
}

bool WriteRadialProfilePlotPng(const std::filesystem::path& path,
                               const RadialProfileParams& params,
                               const RadialProfileResult& result,
                               int width,
                               int height)
{
  if (!result.valid || width <= 64 || height <= 64) return false;

  TrackingVector<unsigned char> rgb(static_cast<size_t>(width) *
                                    static_cast<size_t>(height) * 3,
                                    255);
  const int x0 = 72;
  const int x1 = width - 28;
  const int y0 = 28;
  const int y1 = height - 58;
  const Rgb black{25, 25, 25};
  const Rgb grid{225, 225, 225};
  const Rgb line{30, 105, 210};

  for (int i = 0; i <= 5; ++i) {
    const int x = x0 + (x1 - x0) * i / 5;
    const int y = y0 + (y1 - y0) * i / 5;
    DrawLine(rgb, width, height, x, y0, x, y1, grid);
    DrawLine(rgb, width, height, x0, y, x1, y, grid);
  }
  DrawLine(rgb, width, height, x0, y1, x1, y1, black);
  DrawLine(rgb, width, height, x0, y0, x0, y1, black);

  float xmin = 0.0f, xmax = 1.0f, ymin = 0.0f, ymax = 1.0f;
  if (!MakeAxisRange(params.xmin, params.xmax, params.plotXAxisLog, xmin, xmax)) return false;
  if (!MakeAxisRange(params.ymin, params.ymax, params.plotYAxisLog, ymin, ymax)) return false;

  bool havePrev = false;
  int prevX = 0;
  int prevY = 0;
  const size_t n = std::min(result.x.size(), result.y.size());
  for (size_t i = 0; i < n; ++i) {
    float tx = 0.0f;
    float ty = 0.0f;
    if (!TransformValue(result.x[i], params.plotXAxisLog, tx) ||
        !TransformValue(result.y[i], params.plotYAxisLog, ty) ||
        tx < xmin || tx > xmax || ty < ymin || ty > ymax) {
      havePrev = false;
      continue;
    }
    const int px = MapX(tx, xmin, xmax, x0, x1);
    const int py = MapY(ty, ymin, ymax, y0, y1);
    if (havePrev) {
      DrawLine(rgb, width, height, prevX, prevY, px, py, line);
    }
    FillRect(rgb, width, height, px - 1, py - 1, px + 2, py + 2, line);
    prevX = px;
    prevY = py;
    havePrev = true;
  }

  return WritePngRgb(path.string().c_str(), width, height, rgb);
}

bool WriteHistogram2DPlotPng(const std::filesystem::path& path,
                             const Histogram2DParams& params,
                             const Histogram2DResult& result,
                             int width,
                             int height)
{
  if (!result.valid || width <= 64 || height <= 64) return false;

  TrackingVector<unsigned char> rgb(static_cast<size_t>(width) *
                                    static_cast<size_t>(height) * 3,
                                    255);
  const int x0 = 72;
  const int x1 = width - 28;
  const int y0 = 28;
  const int y1 = height - 58;
  const Rgb black{25, 25, 25};

  float vmin = std::numeric_limits<float>::max();
  float vmax = -std::numeric_limits<float>::max();
  for (const auto& col : result.values) {
    for (float v : col) {
      float tv = 0.0f;
      if (!TransformValue(v, params.logScaleColor, tv)) continue;
      vmin = std::min(vmin, tv);
      vmax = std::max(vmax, tv);
    }
  }
  if (!(vmax > vmin)) {
    vmin = 0.0f;
    vmax = 1.0f;
  }

  const int nx = static_cast<int>(result.values.size());
  const int ny = nx > 0 ? static_cast<int>(result.values[0].size()) : 0;
  if (nx <= 0 || ny <= 0) return false;

  for (int ix = 0; ix < nx; ++ix) {
    for (int iy = 0; iy < ny; ++iy) {
      float tv = vmin;
      if (ix < static_cast<int>(result.values.size()) &&
          iy < static_cast<int>(result.values[static_cast<size_t>(ix)].size())) {
        TransformValue(result.values[static_cast<size_t>(ix)][static_cast<size_t>(iy)],
                       params.logScaleColor,
                       tv);
      }
      const float t = (tv - vmin) / (vmax - vmin);
      const Rgb c = SampleColormap(t);
      const int px0 = x0 + (x1 - x0) * ix / nx;
      const int px1 = x0 + (x1 - x0) * (ix + 1) / nx;
      const int py0 = y1 - (y1 - y0) * (iy + 1) / ny;
      const int py1 = y1 - (y1 - y0) * iy / ny;
      FillRect(rgb, width, height, px0, py0, px1, py1, c);
    }
  }

  DrawLine(rgb, width, height, x0, y1, x1, y1, black);
  DrawLine(rgb, width, height, x0, y0, x0, y1, black);
  DrawLine(rgb, width, height, x1, y0, x1, y1, black);
  DrawLine(rgb, width, height, x0, y0, x1, y0, black);
  return WritePngRgb(path.string().c_str(), width, height, rgb);
}

AnalysisPlotExportResult ExportRadialProfilePlotPackage(
  const AnalysisPlotExportSpec& spec,
  const RadialProfileParams& params,
  const RadialProfileResult& result)
{
  std::string error;
  if (!EnsureDirectory(spec.directory, error)) {
    return MakeExportError("failed to create export directory: " + error);
  }

  AnalysisPlotExportResult out;
  out.jobJsonPath = spec.directory / (spec.stem + ".json");
  out.dataPath = spec.directory / (spec.stem + ".txt");
  out.imagePath = spec.directory / (spec.stem + ".png");

  if (!WriteRadialProfileData(out.dataPath, result)) {
    return MakeExportError("failed to write " + out.dataPath.string());
  }
  if (!WriteRadialProfilePlotPng(out.imagePath, params, result, spec.imageWidth, spec.imageHeight)) {
    return MakeExportError("failed to write " + out.imagePath.string());
  }

  json job;
  job["type"] = "radial_profile";
  job["snapshot"] = SnapshotToJson(spec.snapshot);
  job["camera"] = CameraToJson(spec.camera);
  job["radial"] = RadialParamsToJson(params, out.dataPath, out.imagePath);
  if (!WriteJsonFile(out.jobJsonPath, job)) {
    return MakeExportError("failed to write " + out.jobJsonPath.string());
  }

  out.ok = true;
  return out;
}

AnalysisPlotExportResult ExportHistogram2DPlotPackage(
  const AnalysisPlotExportSpec& spec,
  const Histogram2DParams& params,
  const Histogram2DResult& result)
{
  std::string error;
  if (!EnsureDirectory(spec.directory, error)) {
    return MakeExportError("failed to create export directory: " + error);
  }

  AnalysisPlotExportResult out;
  out.jobJsonPath = spec.directory / (spec.stem + ".json");
  out.dataPath = spec.directory / (spec.stem + ".txt");
  out.imagePath = spec.directory / (spec.stem + ".png");

  if (!WriteHistogram2DData(out.dataPath, result)) {
    return MakeExportError("failed to write " + out.dataPath.string());
  }
  if (!WriteHistogram2DPlotPng(out.imagePath, params, result, spec.imageWidth, spec.imageHeight)) {
    return MakeExportError("failed to write " + out.imagePath.string());
  }

  json job;
  job["type"] = "histogram2d";
  job["snapshot"] = SnapshotToJson(spec.snapshot);
  job["camera"] = CameraToJson(spec.camera);
  job["histogram"] = HistogramParamsToJson(params, out.dataPath, out.imagePath);
  if (!WriteJsonFile(out.jobJsonPath, job)) {
    return MakeExportError("failed to write " + out.jobJsonPath.string());
  }

  out.ok = true;
  return out;
}

AnalysisPlotExportResult ExportBarHistogramPlotPackage(
  const AnalysisPlotExportSpec& spec,
  const BarHistogramPlotExportParams& params,
  const TrackingVector<float>& centers,
  const TrackingVector<float>& values)
{
  std::string error;
  if (!EnsureDirectory(spec.directory, error)) {
    return MakeExportError("failed to create export directory: " + error);
  }

  AnalysisPlotExportResult out;
  out.jobJsonPath = spec.directory / (spec.stem + ".json");
  out.dataPath = spec.directory / (spec.stem + ".txt");
  out.imagePath = spec.directory / (spec.stem + ".png");

  if (!WriteBarHistogramData(out.dataPath, centers, values)) {
    return MakeExportError("failed to write " + out.dataPath.string());
  }
  if (!WriteBarHistogramPlotPng(out.imagePath,
                                params,
                                centers,
                                values,
                                spec.imageWidth,
                                spec.imageHeight)) {
    return MakeExportError("failed to write " + out.imagePath.string());
  }

  json job;
  job["type"] = "plot_bar_histogram";
  job["snapshot"] = SnapshotToJson(spec.snapshot);
  job["camera"] = CameraToJson(spec.camera);
  job["plot"] = BarHistogramParamsToJson(params, out.dataPath, out.imagePath);
  if (!WriteJsonFile(out.jobJsonPath, job)) {
    return MakeExportError("failed to write " + out.jobJsonPath.string());
  }

  out.ok = true;
  return out;
}

AnalysisPlotExportResult ExportLineSeriesPlotPackage(
  const AnalysisPlotExportSpec& spec,
  const LineSeriesPlotExportParams& params,
  const std::vector<PlotLineSeries>& series)
{
  std::string error;
  if (!EnsureDirectory(spec.directory, error)) {
    return MakeExportError("failed to create export directory: " + error);
  }

  AnalysisPlotExportResult out;
  out.jobJsonPath = spec.directory / (spec.stem + ".json");
  out.dataPath = spec.directory / (spec.stem + ".txt");
  out.imagePath = spec.directory / (spec.stem + ".png");

  if (!WriteLineSeriesData(out.dataPath, series)) {
    return MakeExportError("failed to write " + out.dataPath.string());
  }
  if (!WriteLineSeriesPlotPng(out.imagePath,
                              params,
                              series,
                              spec.imageWidth,
                              spec.imageHeight)) {
    return MakeExportError("failed to write " + out.imagePath.string());
  }

  json job;
  job["type"] = "plot_line_series";
  job["snapshot"] = SnapshotToJson(spec.snapshot);
  job["camera"] = CameraToJson(spec.camera);
  job["plot"] = LineSeriesParamsToJson(params, series, out.dataPath, out.imagePath);
  if (!WriteJsonFile(out.jobJsonPath, job)) {
    return MakeExportError("failed to write " + out.jobJsonPath.string());
  }

  out.ok = true;
  return out;
}
