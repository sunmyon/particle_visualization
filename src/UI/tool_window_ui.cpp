#include "tool_window_ui.h"
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include "implot.h"
#include <string>

#include "app/state/runtime_state.h"
#include "app/state/tool_window_state.h"
#include "app/state/analysis_state.h"
#include "app/state/render_runtime_state.h"
#include "app/state/normalization_config.h"
#include "app/state/window_commands.h"
#include <vector>
#include "interaction/camera.h"
#include "render/scene_objects.h"
#include "render/colormap_defs.h"
#include "UI/transfer_function_editor.hpp"
#include "data/simulation_dataset.h"
#include "data/halo_store.h"

#include "projection/make_2D_projection_map.h"
#include "projection/projection_map_params.h"
#include "projection/projection_geometry.h"
#include "analysis/radial_profile.h"
#include "analysis/histogram2d.h"
#include "analysis/profile_histogram_export.h"

#include <algorithm>
#include <cmath>
#include <limits>

extern void UpdateCuboidTransformArcball(CuboidObject& cuboid,
                                         float oldX, float oldY,
                                         float newX, float newY,
					 float screenWidth, float screenHeight,
                                         const glm::mat4& view,
                                         const glm::vec3& pivot);

namespace {
struct ProjectionQuantityRange {
  float min = 1.0e-6f;
  float max = 1.0f;
};

ProjectionQuantityRange ProjectionRangeForQuantity(const QuantityState& quantity,
                                                   QuantityId selected,
                                                   bool positiveOnly)
{
  ProjectionQuantityRange out;
  out.min = std::numeric_limits<float>::max();
  out.max = -std::numeric_limits<float>::max();
  bool valid = false;

  const int qidx = static_cast<int>(selected);
  if (qidx < 0 || qidx >= kMaxQ) {
    return {};
  }

  for (int t = 0; t < kNumTypes; ++t) {
    const float typeMin = quantity.range.valueMin[qidx][t];
    const float typeMax = quantity.range.valueMax[qidx][t];
    if (!std::isfinite(typeMin) || !std::isfinite(typeMax)) continue;
    if (typeMin == 0.0f && typeMax == 0.0f) continue;

    if (positiveOnly) {
      if (typeMax <= 0.0f) continue;
      out.min = std::min(out.min, std::max(typeMin, typeMax * 1.0e-12f));
      out.max = std::max(out.max, typeMax);
    } else {
      out.min = std::min(out.min, typeMin);
      out.max = std::max(out.max, typeMax);
    }
    valid = true;
  }

  if (!valid) {
    return {};
  }
  if (out.max <= out.min) {
    out.max = out.min + std::max(std::abs(out.min) * 1.0e-6f, 1.0e-6f);
  }
  return out;
}

void ApplyProjectionTransferFunction(TransferFunctionEditor& editor,
                                     ProjectionMapParams& params)
{
  params.voronoiTfComponents.clear();
  params.voronoiTfLogDomain = editor.logScale();
  params.voronoiTfValueMin = editor.valueMin();
  params.voronoiTfValueMax = editor.valueMax();

  for (const TFComponent& src : editor.components()) {
    ProjectionTransferFunctionComponent dst;
    dst.type = static_cast<int>(src.type);
    dst.center = src.center;
    dst.width = src.width;
    dst.amplitude = src.amp;
    dst.logDomain = src.logDomain;
    params.voronoiTfComponents.push_back(dst);
  }
}

std::vector<TFComponent>
MakeEditorComponents(const ProjectionMapParams& params)
{
  std::vector<TFComponent> out;
  out.reserve(params.voronoiTfComponents.size());
  for (const ProjectionTransferFunctionComponent& src :
       params.voronoiTfComponents) {
    TFComponent dst;
    dst.type = static_cast<TFShape>(std::clamp(src.type, 0, 2));
    dst.center = src.center;
    dst.width = src.width;
    dst.amp = src.amplitude;
    dst.logDomain = src.logDomain;
    out.push_back(dst);
  }
  return out;
}

void SubmitRadialProfileRequest(const RadialProfileUIState& state,
                                RadialProfileRequestState& request)
{
  request.params = state.draftParams;
  request.params.useOriginal = true;
  request.runRequested = true;
}

void SubmitHistogram2DRequest(const Histogram2DUIState& state,
                              Histogram2DRequestState& request)
{
  request.params = state.draftParams;
  request.runRequested = true;
}

void SubmitProjectionMapParamsRequest(const ProjectionMapParams& draftParams,
                                      ProjectionMapRequestState& request,
                                      bool renderRequested)
{
  request.params = draftParams;
  request.paramsChanged = true;
  request.renderRequested = request.renderRequested || renderRequested;
}

void SubmitTopParticleQueryRequest(const TopParticlesUIState& state,
                                   TopParticlesRequestState& request)
{
  request.queryParticleId = state.queryID;
  request.queryParticleRequested = true;
}

void SubmitTopParticleFilterRequest(const TopParticlesUIState& state,
                                    TopParticlesRequestState& request)
{
  for (int i = 0; i < 6; ++i) {
    request.selectedTypes[i] = state.selectType[i];
  }
  request.topCount = state.m;
  request.sortQuantity = state.sortQuantity;
  request.sortDescending = state.sortDescending;
  request.refreshFilteredRequested = true;
}

void SubmitTopParticleHistogramRequest(const TopParticlesUIState& state,
                                       TopParticlesRequestState& request)
{
  request.histogramSelectedVar = state.selectedVar;
  request.histogramBins = state.bins;
  request.histogramLogScaleX = state.histogramLogScaleX;
  request.histogramLogScaleY = state.histogramLogScaleY;
  request.histogramAutoRange = state.autoRange;
  request.histogramRange1Min = state.range1_min;
  request.histogramRange1Max = state.range1_max;
  request.histogramRange2Min = state.range2_min;
  request.histogramRange2Max = state.range2_max;
  request.histogramUseCameraCenter = state.useCameraCenter;
  request.histogramCameraRadius = state.cameraRadius;
  for (int i = 0; i < 6; ++i) {
    request.selectedTypes[i] = state.selectType[i];
  }
  request.computeHistogramRequested = true;
}

void SubmitHaloLoadRequest(const HaloesUIState& state,
                           HaloesRequestState& request,
                           bool loadIds)
{
  std::snprintf(request.filename,
                sizeof(request.filename),
                "%s",
                state.fname);
  request.loadWithoutIdsRequested = !loadIds;
  request.loadWithIdsRequested = loadIds;
}

void SubmitHaloRecomputeRequest(const HaloesUIState& state,
                                HaloesRequestState& request)
{
  request.recomputeUseMassWeight = state.recomputeUseMassWeight;
  request.recomputeUseOriginalPos = state.recomputeUseOriginalPos;
  request.recomputeMinParticles = state.recomputeMinParticles;
  request.recomputePositionsRequested = true;
}

void SubmitHaloStressSelectionRequest(const HaloesUIState& state,
                                      HaloesRequestState& request)
{
  request.selectedForStress = state.selectedForStress;
  request.stressSelectionChanged = true;
}

void SubmitHaloHistogramRequest(const HaloesUIState& state,
                                HaloesRequestState& request)
{
  request.histogramSelectedVar = state.selectedVar;
  request.histogramBins = state.bins;
  request.histogramLogScaleX = state.histogramLogScaleX;
  request.histogramLogScaleY = state.histogramLogScaleY;
  request.histogramAutoRange = state.autoRange;
  request.histogramRange1Min = state.range1_min;
  request.histogramRange1Max = state.range1_max;
  request.histogramRange2Min = state.range2_min;
  request.histogramRange2Max = state.range2_max;
  request.computeHistogramRequested = true;
}

std::string MakePlotExportTimestamp()
{
  std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  char buf[32] = "";
  std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &local);
  return buf;
}

std::filesystem::path EnsurePlotExportFolder(char* folder, size_t folderSize)
{
  if (folder && folder[0] != '\0') {
    return std::filesystem::path(folder);
  }

  const std::filesystem::path dir =
    std::filesystem::temp_directory_path() /
    ("particle_vis_implot_exports_" + MakePlotExportTimestamp());
  if (folder && folderSize > 0) {
    std::snprintf(folder, folderSize, "%s", dir.string().c_str());
  }
  return dir;
}

AnalysisPlotExportSpec MakePlotExportSpec(const PlotBatchExportViewContext& ctx,
                                          const std::filesystem::path& dir,
                                          const std::string& stem)
{
  AnalysisPlotExportSpec spec;
  spec.directory = dir;
  spec.stem = stem;
  spec.snapshot.folderPath = ctx.snapshotFolderPath ? ctx.snapshotFolderPath : "";
  spec.snapshot.fileFormat = ctx.snapshotFileFormat ? ctx.snapshotFileFormat : "";
  spec.snapshot.initialIndex = ctx.initialIndex;
  spec.snapshot.currentStep = ctx.currentStep;
  spec.snapshot.skipStep = ctx.skipStep;
  spec.snapshot.batchSize = ctx.batchSize;
  spec.snapshot.useHDF5 = ctx.useHDF5;
  spec.camera.position = glm::vec3(ctx.cameraPosition[0],
                                   ctx.cameraPosition[1],
                                   ctx.cameraPosition[2]);
  spec.camera.target = glm::vec3(ctx.cameraTarget[0],
                                 ctx.cameraTarget[1],
                                 ctx.cameraTarget[2]);
  return spec;
}

void StoreExportStatus(char* status,
                       size_t statusSize,
                       const AnalysisPlotExportResult& result)
{
  if (!status || statusSize == 0) return;
  if (result.ok) {
    std::snprintf(status,
                  statusSize,
                  "Saved %s",
                  result.jobJsonPath.string().c_str());
  } else {
    std::snprintf(status, statusSize, "Export failed: %s", result.error.c_str());
  }
}

void ExportRadialProfileIfNeeded(RadialProfileUIState& state,
                                 const RadialProfileResultState& result,
                                 const PlotBatchExportViewContext& ctx)
{
  if (!state.exportPlotPackage ||
      !result.computed ||
      !result.result.valid ||
      result.version == state.lastExportedVersion) {
    return;
  }

  const std::filesystem::path dir =
    EnsurePlotExportFolder(state.exportFolder, sizeof(state.exportFolder));
  const std::string stem =
    "radial_profile_" + std::to_string(static_cast<unsigned long long>(result.version));
  AnalysisPlotExportSpec spec = MakePlotExportSpec(ctx, dir, stem);
  AnalysisPlotExportResult exportResult =
    ExportRadialProfilePlotPackage(spec, result.paramsUsed, result.result);
  StoreExportStatus(state.lastExportStatus, sizeof(state.lastExportStatus), exportResult);
  if (exportResult.ok) {
    state.lastExportedVersion = result.version;
  }
}

void ExportHistogram2DIfNeeded(Histogram2DUIState& state,
                               const Histogram2DResultState& result,
                               const PlotBatchExportViewContext& ctx)
{
  if (!state.exportPlotPackage ||
      !result.computed ||
      !result.result.valid ||
      result.version == state.lastExportedVersion) {
    return;
  }

  const std::filesystem::path dir =
    EnsurePlotExportFolder(state.exportFolder, sizeof(state.exportFolder));
  const std::string stem =
    "histogram2d_" + std::to_string(static_cast<unsigned long long>(result.version));
  AnalysisPlotExportSpec spec = MakePlotExportSpec(ctx, dir, stem);
  Histogram2DParams paramsForBatch = result.paramsUsed;
  paramsForBatch.cameraRadius *= ctx.renderToWorldScale;
  AnalysisPlotExportResult exportResult =
    ExportHistogram2DPlotPackage(spec, paramsForBatch, result.result);
  StoreExportStatus(state.lastExportStatus, sizeof(state.lastExportStatus), exportResult);
  if (exportResult.ok) {
    state.lastExportedVersion = result.version;
  }
}

void ExportTopParticleHistogramIfNeeded(TopParticlesUIState& state,
                                        const TopParticlesResultState& result,
                                        const PlotBatchExportViewContext& ctx)
{
  if (!state.exportHistogramPackage ||
      !result.histogramComputed ||
      result.histogramVersion == state.lastExportedHistogramVersion) {
    return;
  }

  static constexpr const char* kQuantities[] = {
    "x", "y", "z", "r", "Density", "Temperature", "Hsml", "Mass"
  };
  const int qIndex = std::clamp(state.selectedVar, 0, static_cast<int>(IM_ARRAYSIZE(kQuantities)) - 1);

  BarHistogramPlotExportParams params;
  params.kind = "top_particle_histogram";
  params.title = "Top Particle Histogram";
  params.xLabel = kQuantities[qIndex];
  params.yLabel = "Count";
  params.logX = state.histogramLogScaleX;
  params.logY = state.histogramLogScaleY;
  params.xMin = state.range1_min;
  params.xMax = state.range1_max;
  params.yMin = state.range2_min;
  params.yMax = state.range2_max;
  params.binSize = result.binSize;

  const std::filesystem::path dir =
    EnsurePlotExportFolder(state.exportFolder, sizeof(state.exportFolder));
  const std::string stem =
    "top_particle_histogram_" +
    std::to_string(static_cast<unsigned long long>(result.histogramVersion));
  AnalysisPlotExportSpec spec = MakePlotExportSpec(ctx, dir, stem);
  AnalysisPlotExportResult exportResult =
    ExportBarHistogramPlotPackage(spec, params, result.binCenters, result.histBins);
  StoreExportStatus(state.lastExportStatus, sizeof(state.lastExportStatus), exportResult);
  if (exportResult.ok) {
    state.lastExportedHistogramVersion = result.histogramVersion;
  }
}

void ExportHaloHistogramIfNeeded(HaloesUIState& state,
                                 const PlotBatchExportViewContext& ctx)
{
  if (!state.exportHistogramPackage ||
      !state.histogramComputed ||
      state.histogramVersion == state.lastExportedHistogramVersion) {
    return;
  }

  static constexpr const char* kQuantities[] = {
    "Mass", "GasMass", "StellarMass", "GasMetallicity", "StellarMetallicity"
  };
  const int qIndex = std::clamp(state.selectedVar, 0, static_cast<int>(IM_ARRAYSIZE(kQuantities)) - 1);

  BarHistogramPlotExportParams params;
  params.kind = "halo_histogram";
  params.title = "Halo Histogram";
  params.xLabel = kQuantities[qIndex];
  params.yLabel = "Count";
  params.logX = state.histogramLogScaleX;
  params.logY = state.histogramLogScaleY;
  params.xMin = state.range1_min;
  params.xMax = state.range1_max;
  params.yMin = state.range2_min;
  params.yMax = state.range2_max;
  params.binSize = state.binSize;

  const std::filesystem::path dir =
    EnsurePlotExportFolder(state.exportFolder, sizeof(state.exportFolder));
  const std::string stem =
    "halo_histogram_" +
    std::to_string(static_cast<unsigned long long>(state.histogramVersion));
  AnalysisPlotExportSpec spec = MakePlotExportSpec(ctx, dir, stem);
  AnalysisPlotExportResult exportResult =
    ExportBarHistogramPlotPackage(spec, params, state.binCenters, state.histBins);
  StoreExportStatus(state.lastExportStatus, sizeof(state.lastExportStatus), exportResult);
  if (exportResult.ok) {
    state.lastExportedHistogramVersion = state.histogramVersion;
  }
}
}

void DrawRadialProfileUI(RadialProfileUIState& state,
                         RadialProfileRequestState& request,
                         const RadialProfileResultState& result,
                         const RadialProfileViewContext& ctx)
{
  if (!state.open) return;
  const QuantityState& quantity = ctx.quantity;
  auto& params = state.draftParams;

  ImGui::Begin("Radial Profile", &state.open);

  const char* xaxes[] = { "r", "x", "y", "z", "M(<r)" };
  ImGui::Combo("X Axis", &state.selectedXAxis, xaxes, IM_ARRAYSIZE(xaxes));
  params.xmode = (XAxisMode)state.selectedXAxis;

  const int baseCount = quantity.catalog.nUIQ;
  const bool allowMDot = (params.xmode == XAxisMode::Radius ||
                          params.xmode == XAxisMode::EnclosedMass);
  const int totalCount = baseCount + (allowMDot ? 1 : 0);

  if (state.selectedVarIdx < 0 || state.selectedVarIdx >= totalCount)
    state.selectedVarIdx = 0;

  auto labelAt = [&](int idx)->const char* {
    if (idx < baseCount) return QuantityLabel(quantity.catalog.uiQ[idx]);
    return "mdot";
  };

  if (ImGui::BeginCombo("Quantity", labelAt(state.selectedVarIdx))) {
    for (int i = 0; i < baseCount; ++i) {
      bool sel = (state.selectedVarIdx == i);
      if (ImGui::Selectable(QuantityLabel(quantity.catalog.uiQ[i]), sel))
        state.selectedVarIdx = i;
      if (sel) ImGui::SetItemDefaultFocus();
    }
    if (allowMDot) {
      ImGui::Separator();
      int mdotIdx = baseCount;
      bool sel = (state.selectedVarIdx == mdotIdx);
      if (ImGui::Selectable("mdot", sel))
        state.selectedVarIdx = mdotIdx;
      if (sel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  if (state.selectedVarIdx < baseCount) {
    params.var1 = quantity.catalog.uiQ[state.selectedVarIdx];
    params.isMDot = false;
  } else {
    params.isMDot = true;
  }

  ImGui::InputInt("Number of Bins", &params.bins);
  params.useOriginal = true;
  ImGui::Checkbox("Log X Axis", &params.plotXAxisLog);
  ImGui::Checkbox("Log Y Axis", &params.plotYAxisLog);
  ImGui::Checkbox("Auto Range", &params.autorange);
  ImGui::Checkbox("Take absolute value", &params.flagAbsolute);

  if (!params.autorange) {
    ImGui::InputFloat("X Axis Min", &params.xmin, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("X Axis Max", &params.xmax, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Min", &params.ymin, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Max", &params.ymax, 0.0f, 0.0f, "%g");
  }

  ImGui::InputFloat("Maximum Radius (cut)", &params.rmax, 0.0f, 0.0f, "%g");
  ImGui::Checkbox("Save plot image + JSON after compute", &state.exportPlotPackage);
  if (state.exportFolder[0] != '\0') {
    ImGui::TextWrapped("Export folder: %s", state.exportFolder);
  }
  if (state.lastExportStatus[0] != '\0') {
    ImGui::TextWrapped("%s", state.lastExportStatus);
  }

  if (ImGui::Button("Compute profile")) {
    SubmitRadialProfileRequest(state, request);
  }

  if (result.computed && result.result.valid) {
    if (ImPlot::BeginPlot("Profile", ImVec2(-1, 300))) {
      const char* ylabel = params.isMDot ? "mdot" : QuantityLabel(params.var1);
      ImPlot::SetupAxes(XAxisLabel(params.xmode), ylabel);

      if (params.plotXAxisLog)
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
      if (params.plotYAxisLog)
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);

      ImPlot::SetupAxisLimits(ImAxis_X1, params.xmin, params.xmax, ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, params.ymin, params.ymax, ImGuiCond_Always);

      ImPlot::PlotLine("Profile",
                       result.result.x.data(),
                       result.result.y.data(),
                       (int)result.result.y.size());
      ImPlot::EndPlot();
    }
  }
  ExportRadialProfileIfNeeded(state, result, ctx.exportContext);

  ImGui::End();
}

void DrawHistogram2DUI(Histogram2DUIState& state,
                       Histogram2DRequestState& request,
                       const Histogram2DResultState& result,
                       const Histogram2DViewContext& ctx)
{
  if (!state.open) return;
  const QuantityCatalogState& catalog = ctx.catalog;
  auto& params = state.draftParams;

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
  ImGui::Begin("histogram 2D", &state.open, ImGuiWindowFlags_None);

  if (ImGui::BeginCombo("X Axis Quantity", QuantityLabel(params.var1))) {
    for (int q = 0; q < catalog.nAllQ; ++q) {
      QuantityId cand = catalog.allQ[q];
      bool is_selected = (cand == params.var1);
      if (ImGui::Selectable(QuantityLabel(cand), is_selected))
        params.var1 = cand;
      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  if (ImGui::BeginCombo("Y Axis Quantity", QuantityLabel(params.var2))) {
    for (int q = 0; q < catalog.nAllQ; ++q) {
      QuantityId cand = catalog.allQ[q];
      bool is_selected = (cand == params.var2);
      if (ImGui::Selectable(QuantityLabel(cand), is_selected))
        params.var2 = cand;
      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  ImGui::InputInt("Bins X", &params.bins1);
  ImGui::InputInt("Bins Y", &params.bins2);

  ImGui::Checkbox("Use Log scale X", &params.logScaleX);
  ImGui::Checkbox("Use Log scale Y", &params.logScaleY);
  ImGui::Checkbox("Use Log color scale", &params.logScaleColor);

  ImGui::Checkbox("Auto Range", &params.autoRange);

  if (!params.autoRange) {
    ImGui::InputFloat("X Axis Min", &params.range1_min, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("X Axis Max", &params.range1_max, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Min", &params.range2_min, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Max", &params.range2_max, 0.0f, 0.0f, "%g");
  }

#ifdef USE_CONVEX_HULL
  ImGui::Checkbox("Filter: Use Convex Hull", &params.useConvexHull);
#endif

  ImGui::Checkbox("Filter: Use Camera Center", &params.useCameraCenter);
  if (params.useCameraCenter) {
    ImGui::InputFloat("Camera Radius", &params.cameraRadius, 0.1f, 1.0f, "%.2f");
  }

  if (ImGui::Button("Compute Histogram")) {
    SubmitHistogram2DRequest(state, request);
  }
  ImGui::Checkbox("Save plot image + JSON after compute", &state.exportPlotPackage);
  if (state.exportFolder[0] != '\0') {
    ImGui::TextWrapped("Export folder: %s", state.exportFolder);
  }
  if (state.lastExportStatus[0] != '\0') {
    ImGui::TextWrapped("%s", state.lastExportStatus);
  }

  if (result.computed && result.result.valid) {
    size_t computedBins1 = result.result.values.size();
    size_t computedBins2 = (computedBins1 > 0) ? result.result.values[0].size() : 0;

    std::vector<float> heatmapData;
    heatmapData.reserve(computedBins1 * computedBins2);

    for (size_t j = 0; j < computedBins2; j++) {
      for (size_t i = 0; i < computedBins1; i++) {
        heatmapData.push_back(result.result.values[i][j]);
      }
    }

    if (ImPlot::BeginPlot("2D Histogram", ImVec2(-1, 300))) {
      ImPlot::SetupAxes(QuantityLabel(params.var1), QuantityLabel(params.var2));

      ImPlot::SetupAxisLimits(ImAxis_X1,
                              params.range1_min,
                              params.range1_max,
                              ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1,
                              params.range2_min,
                              params.range2_max,
                              ImGuiCond_Always);

      ImPlot::PushColormap(ImPlotColormap_Viridis);

      ImPlot::PlotHeatmap("Histogram",
                          heatmapData.data(),
                          (int)computedBins2,
                          (int)computedBins1,
                          0, 0, "",
                          ImPlotPoint(params.range1_min, params.range2_min),
                          ImPlotPoint(params.range1_max, params.range2_max));

      ImPlot::EndPlot();
    }
  }
  ExportHistogram2DIfNeeded(state, result, ctx.exportContext);

  ImGui::End();
}


void DrawProjectionFontSelectionUI(ProjectionMapUIState& state,
                                   ProjectionFontSelectionRequestState& request)
{
  if (!state.fontWindowOpen) return;

  ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_Appearing);
  ImGui::Begin("Font Selection Preview", &state.fontWindowOpen, ImGuiWindowFlags_None);

  const int fontCount = static_cast<int>(state.availableFontPaths.size());
  if (fontCount == 0) {
    ImGui::Text("No fonts available.");
    ImGui::End();
    return;
  }

  if (state.currentFontIndex < 0 || state.currentFontIndex >= fontCount) {
    state.currentFontIndex = 0;
  }

  if (ImGui::BeginCombo("Select Font",
                        state.availableFontPaths[state.currentFontIndex].c_str())) {
    for (int i = 0; i < fontCount; i++) {
      bool isSelected = (state.currentFontIndex == i);
      if (ImGui::Selectable(state.availableFontPaths[i].c_str(), isSelected)) {
        state.currentFontIndex = i;
      }
      if (isSelected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  ImGui::Text("Selected Font: %s",
              state.availableFontPaths[state.currentFontIndex].c_str());

  if (ImGui::Button("Apply Font")) {
    request.applySelectedFontRequested = true;
  }

  if (state.appliedFontIndex >= 0 &&
      state.appliedFontIndex < fontCount) {
    ImGui::Text("Applied Font: %s",
                state.availableFontPaths[state.appliedFontIndex].c_str());
  } else {
    ImGui::TextUnformatted("Applied Font: default");
  }

  ImGui::TextUnformatted("The quick brown fox jumps over the lazy dog.");

  ImGui::End();
}

bool DrawProjectionQuantityCombo(const char* label,
                                 QuantityId& quantity,
                                 const QuantityCatalogState& catalog)
{
  bool dirty = false;
  if (ImGui::BeginCombo(label, QuantityLabel(quantity))) {
    for (int q = 0; q < catalog.nUIQ; ++q) {
      QuantityId cand = catalog.uiQ[q];
      bool isSelected = (cand == quantity);
      if (ImGui::Selectable(QuantityLabel(cand), isSelected)) {
        quantity = cand;
        dirty = true;
      }
      if (isSelected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return dirty;
}

bool DrawProjectionAxisCombo(const char* label, int& selectedAxis)
{
  const char* axisLabels[] = { "X", "Y", "Z" };
  selectedAxis = std::clamp(selectedAxis, 0, 2);
  return ImGui::Combo(label, &selectedAxis, axisLabels, IM_ARRAYSIZE(axisLabels));
}

bool DrawProjectionSignCombo(const char* label, int& sign)
{
  const char* signLabels[] = { "+", "-" };
  int value = sign < 0 ? 1 : 0;
  const bool changed = ImGui::Combo(label, &value, signLabels, IM_ARRAYSIZE(signLabels));
  if (changed) {
    sign = value == 1 ? -1 : 1;
  }
  return changed;
}

void DrawProjectionSectionHeader(const char* label)
{
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  ImGui::SetWindowFontScale(1.15f);
  ImGui::TextUnformatted(label);
  ImGui::SetWindowFontScale(1.0f);
}

int ProjectionPanelGridCapacity(const ProjectionMapParams& params)
{
  return std::min(params.multiPanelRows * params.multiPanelCols,
                  kProjectionMaxPanels);
}

void ProjectionSyncPanelCountFromGrid(ProjectionMapParams& params)
{
  params.multiPanelRows = std::clamp(params.multiPanelRows, 1, 3);
  params.multiPanelCols = std::clamp(params.multiPanelCols, 1, 6);
  params.multiPanelCount = ProjectionPanelGridCapacity(params);
  params.selectedPanelIndex =
    std::clamp(params.selectedPanelIndex, 0, params.multiPanelCount - 1);
}

void ProjectionCopySeedPanels(ProjectionMapParams& params, int oldCount)
{
  const int newCount = ProjectionPanelGridCapacity(params);
  if (newCount > oldCount) {
    const ProjectionPanelSpec seed =
      params.panels[std::max(0, oldCount - 1)];
    for (int i = oldCount; i < newCount; ++i) {
      params.panels[i] = seed;
    }
  }
  ProjectionSyncPanelCountFromGrid(params);
}

bool DrawProjectionLayoutEditor(ProjectionMapParams& params,
                                const ProjectionMapViewContext& ctx,
                                float renderToWorld,
                                float worldToRender)
{
  static TransferFunctionEditor voronoiTransferEditor;
  bool dirty = false;
  if (!params.layoutEditorOpen) return false;

  ProjectionEnsureLayoutInitialized(params);

  ImGui::SetNextWindowSize(ImVec2(1080, 520), ImGuiCond_Appearing);
  ImGui::Begin("Projection layout editor", &params.layoutEditorOpen);
  ImGui::SetWindowFontScale(1.0f);

  if (ImGui::BeginTabBar("projection_layout_tabs")) {
    if (ImGui::BeginTabItem("Panels")) {
      ProjectionSyncPanelCountFromGrid(params);
      const int gridPanelCount = ProjectionPanelGridCapacity(params);
      if (!params.multiPanelEnabled) {
        ImGui::TextDisabled("Enable Multi panel in the main window to add panels.");
      }

      const int panelCount = params.multiPanelEnabled ? gridPanelCount : 1;
      std::vector<const char*> blockNames;
      blockNames.reserve(static_cast<size_t>(params.viewBlockCount));
      for (int i = 0; i < params.viewBlockCount; ++i) {
        blockNames.push_back(params.viewBlocks[i].name);
      }

      const ColormapDef* colormaps = AvailableColormaps();
      const int colormapCount = AvailableColormapCount();
      const ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_SizingFixedFit;
      if (ImGui::BeginTable("projection_panels_table",
                            7,
                            tableFlags,
                            ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34.0f);
        ImGui::TableSetupColumn("Block", ImGuiTableColumnFlags_WidthFixed, 125.0f);
        ImGui::TableSetupColumn("Quantity", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 460.0f);
        ImGui::TableSetupColumn("Stars", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Vector", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Labels", ImGuiTableColumnFlags_WidthFixed, 86.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (int i = 0; i < panelCount; ++i) {
          ProjectionPanelSpec& panel = params.panels[i];
          panel.viewBlockIndex =
            std::clamp(panel.viewBlockIndex, 0, params.viewBlockCount - 1);
          panel.colormapIndex =
            std::clamp(panel.colormapIndex, 0, colormapCount - 1);
          ImGui::PushID(i);
          ImGui::TableNextRow();
          if (params.selectedPanelIndex == i) {
            ImGui::TableSetBgColor(
              ImGuiTableBgTarget_RowBg0,
              ImGui::GetColorU32(ImGuiCol_Header));
          }
          ImGui::TableSetColumnIndex(0);
          char panelLabel[32];
          std::snprintf(panelLabel, sizeof(panelLabel), "%d", i + 1);
          if (ImGui::Selectable(panelLabel,
                                params.selectedPanelIndex == i,
                                0,
                                ImVec2(24.0f, 0.0f))) {
            params.selectedPanelIndex = i;
            params.activeViewBlockIndex = panel.viewBlockIndex;
            params.selectedViewBlockIndex = panel.viewBlockIndex;
            ProjectionSyncTopLevelFromViewBlock(params, panel.viewBlockIndex);
            dirty = true;
          }

          ImGui::TableSetColumnIndex(1);
          ImGui::SetNextItemWidth(120.0f);
          if (ImGui::Combo("##block",
                           &panel.viewBlockIndex,
                           blockNames.data(),
                           static_cast<int>(blockNames.size()))) {
            params.selectedPanelIndex = i;
            params.activeViewBlockIndex = panel.viewBlockIndex;
            params.selectedViewBlockIndex = panel.viewBlockIndex;
            ProjectionSyncTopLevelFromViewBlock(params, panel.viewBlockIndex);
            dirty = true;
          }

          ImGui::TableSetColumnIndex(2);
          ImGui::SetNextItemWidth(140.0f);
          dirty |= DrawProjectionQuantityCombo("##quantity",
                                               panel.quantity,
                                               ctx.quantity.catalog);

          ImGui::TableSetColumnIndex(3);
          ImGui::SetNextItemWidth(86.0f);
          if (ImGui::BeginCombo("##colormap", colormaps[panel.colormapIndex].name)) {
            for (int cmap = 0; cmap < colormapCount; ++cmap) {
              const bool selected = panel.colormapIndex == cmap;
              if (ImGui::Selectable(colormaps[cmap].name, selected)) {
                panel.colormapIndex = cmap;
                dirty = true;
              }
              if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
          }
          ImGui::SameLine();
          dirty |= ImGui::Checkbox("Log##log", &panel.flagLogScale);
          ImGui::SameLine();
          dirty |= ImGui::Checkbox("Auto##auto", &panel.autoRange);
          if (!panel.autoRange) {
            ImGui::TextUnformatted("Min");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            dirty |= ImGui::InputFloat("##min", &panel.rangeMin, 0.0f, 0.0f, "%g");
            ImGui::SameLine();
            ImGui::TextUnformatted("Max");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            dirty |= ImGui::InputFloat("##max", &panel.rangeMax, 0.0f, 0.0f, "%g");
          }

          ImGui::TableSetColumnIndex(4);
          const char* starLabels[] = {
            "Off",
            "Star field 1",
            "Star field 2",
            "Star field 3",
            "Star field 4"
          };
          panel.starOverlayIndex =
            std::clamp(panel.starOverlayIndex,
                       0,
                       params.starOverlayCount);
          ImGui::SetNextItemWidth(120.0f);
          if (ImGui::Combo("##star_overlay",
                           &panel.starOverlayIndex,
                           starLabels,
                           params.starOverlayCount + 1)) {
            dirty = true;
          }

          ImGui::TableSetColumnIndex(5);
          const char* vectorLabels[] = {
            "Off",
            "Vector field 1",
            "Vector field 2",
            "Vector field 3",
            "Vector field 4"
          };
          panel.vectorOverlayIndex =
            std::clamp(panel.vectorOverlayIndex,
                       0,
                       params.vectorOverlayCount);
          ImGui::SetNextItemWidth(120.0f);
          if (ImGui::Combo("##vector_overlay",
                           &panel.vectorOverlayIndex,
                           vectorLabels,
                           params.vectorOverlayCount + 1)) {
            dirty = true;
          }

          ImGui::TableSetColumnIndex(6);
          const ProjectionViewBlockSpec resolvedBlock =
            ProjectionResolveViewBlock(params, panel.viewBlockIndex);
          bool showTime = ProjectionResolveLabelMode(
            panel.timeLabelMode,
            resolvedBlock.showTimeLabelDefault);
          if (ImGui::Checkbox("Time##time_label", &showTime)) {
            panel.timeLabelMode = showTime
              ? ProjectionPanelLabelMode::Show
              : ProjectionPanelLabelMode::Hide;
            dirty = true;
          }
          bool showScale = ProjectionResolveLabelMode(
            panel.scaleBarMode,
            resolvedBlock.showScaleDefault);
          if (ImGui::Checkbox("Scale##scale_bar", &showScale)) {
            panel.scaleBarMode = showScale
              ? ProjectionPanelLabelMode::Show
              : ProjectionPanelLabelMode::Hide;
            dirty = true;
          }
          ImGui::PopID();
        }
        ImGui::EndTable();
      }

      params.selectedPanelIndex =
        std::clamp(params.selectedPanelIndex, 0, panelCount - 1);
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("View blocks")) {
      ImGui::SetNextItemWidth(120.0f);
      int blockCount = params.viewBlockCount;
      if (ImGui::InputInt("Block count", &blockCount)) {
        const int oldCount = params.viewBlockCount;
        params.viewBlockCount = std::clamp(blockCount,
                                           1,
                                           kProjectionMaxViewBlocks);
        for (int i = oldCount; i < params.viewBlockCount; ++i) {
          params.viewBlocks[i] = params.viewBlocks[params.activeViewBlockIndex];
          ProjectionSetDefaultViewBlockName(params, i);
          params.viewBlocks[i].sizeSameAsMain = true;
          params.viewBlocks[i].centerSameAsMain = true;
          params.viewBlocks[i].tiltSameAsMain = true;
        }
        dirty = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("Duplicate active") &&
          params.viewBlockCount < kProjectionMaxViewBlocks) {
        const int src = std::clamp(params.selectedViewBlockIndex,
                                   0,
                                   params.viewBlockCount - 1);
        const int dst = params.viewBlockCount++;
        params.viewBlocks[dst] = params.viewBlocks[src];
        ProjectionSetDefaultViewBlockName(params, dst);
        params.viewBlocks[dst].sizeSameAsMain = true;
        params.viewBlocks[dst].centerSameAsMain = true;
        params.viewBlocks[dst].tiltSameAsMain = true;
        params.selectedViewBlockIndex = dst;
        params.activeViewBlockIndex = dst;
        dirty = true;
      }

      for (int i = 0; i < params.viewBlockCount; ++i) {
        ImGui::PushID(i);
        if (ImGui::Selectable(params.viewBlocks[i].name,
                              params.selectedViewBlockIndex == i)) {
          params.selectedViewBlockIndex = i;
          params.activeViewBlockIndex = i;
          ProjectionSyncTopLevelFromViewBlock(params, i);
          dirty = true;
        }
        ImGui::PopID();
      }

      params.selectedViewBlockIndex =
        std::clamp(params.selectedViewBlockIndex, 0, params.viewBlockCount - 1);
      ProjectionViewBlockSpec& block =
        params.viewBlocks[params.selectedViewBlockIndex];
      DrawProjectionSectionHeader("Selected block");
      dirty |= ImGui::InputText("Name", block.name, IM_ARRAYSIZE(block.name));
      dirty |= ImGui::InputInt("Pixels", &block.npixel, 10, 1000);
      block.npixel = std::max(block.npixel, 1);
      const bool isMainBlock = params.selectedViewBlockIndex == 0;
      if (!isMainBlock) {
        dirty |= ImGui::Checkbox("Size same as main", &block.sizeSameAsMain);
        dirty |= ImGui::Checkbox("Center same as main", &block.centerSameAsMain);
        dirty |= ImGui::Checkbox("Tilt/axis same as main", &block.tiltSameAsMain);
      }

      if (isMainBlock || !block.sizeSameAsMain) {
        float xlenOriginal[3] = {
          block.xlen[0] * renderToWorld,
          block.xlen[1] * renderToWorld,
          block.xlen[2] * renderToWorld
        };
        if (ImGui::InputFloat3("Size", xlenOriginal)) {
          for (int k = 0; k < 3; ++k) block.xlen[k] = xlenOriginal[k] * worldToRender;
          dirty = true;
        }
      }

      if (isMainBlock || !block.centerSameAsMain) {
        float xoffsetOriginal[3] = {
          block.xoffset[0] * renderToWorld,
          block.xoffset[1] * renderToWorld,
          block.xoffset[2] * renderToWorld
        };
        if (ImGui::InputFloat3("Center offset", xoffsetOriginal)) {
          for (int k = 0; k < 3; ++k) block.xoffset[k] = xoffsetOriginal[k] * worldToRender;
          dirty = true;
        }
      }
      if (ImGui::Button("Set center from camera")) {
        ProjectionViewBlockSpec& main = params.viewBlocks[0];
        ProjectionViewBlockSpec& centerTarget =
          (!isMainBlock && block.centerSameAsMain) ? main : block;
        centerTarget.xoffset[0] = ctx.camera.cameraTarget.x;
        centerTarget.xoffset[1] = ctx.camera.cameraTarget.y;
        centerTarget.xoffset[2] = ctx.camera.cameraTarget.z;
        dirty = true;
      }

      if (isMainBlock || !block.tiltSameAsMain) {
        ImGui::SetNextItemWidth(120.0f);
        dirty |= DrawProjectionAxisCombo("Projection normal", block.selectedAxis);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(64.0f);
        dirty |= DrawProjectionSignCombo("View sign##projection_sign",
                                         block.projectionSign);
        ImGui::SetNextItemWidth(120.0f);
        dirty |= DrawProjectionAxisCombo("Up direction", block.upAxis);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(64.0f);
        dirty |= DrawProjectionSignCombo("Up sign##up_sign", block.upSign);
        dirty |= ImGui::InputFloat3("Tilt (deg)", block.tilt);
      }
      dirty |= ImGui::Checkbox("Default time label", &block.showTimeLabelDefault);
      dirty |= ImGui::Checkbox("Default spatial scale", &block.showScaleDefault);
      if (block.showScaleDefault) {
        ImGui::SetNextItemWidth(120.0f);
        dirty |= ImGui::InputFloat("Default arrow length",
                                   &block.arrowLenXDefault,
                                   0.0f,
                                   0.0f,
                                   "%g");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        dirty |= ImGui::InputText("Default arrow label",
                                  block.arrowLabelStrDefault,
                                  IM_ARRAYSIZE(block.arrowLabelStrDefault));
      }
      if (dirty && params.selectedViewBlockIndex == params.activeViewBlockIndex) {
        ProjectionSyncTopLevelFromViewBlock(params, params.activeViewBlockIndex);
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Rendering")) {
      dirty |= ImGui::Checkbox("Density weighting", &params.flagDensityWeight);
      dirty |= ImGui::Checkbox("Use Voronoi tesselation", &params.flagVoronoi);
      if (params.flagVoronoi) {
        int voronoiMode = static_cast<int>(params.voronoiMode);
        const char* modeLabels[] = { "Weighted mean", "Opacity rendering" };
        if (ImGui::Combo("Voronoi mode", &voronoiMode, modeLabels, IM_ARRAYSIZE(modeLabels))) {
          params.voronoiMode = static_cast<ProjectionVoronoiMode>(voronoiMode);
          dirty = true;
        }
        dirty |= ImGui::InputInt("nz", &params.step_z, 10, 1000);
        if (params.voronoiMode == ProjectionVoronoiMode::OpacityRendering) {
          dirty |= DrawProjectionQuantityCombo("Opacity quantity",
                                               params.voronoiOpacityVarGas,
                                               ctx.quantity.catalog);
          const ProjectionQuantityRange tfRange =
            ProjectionRangeForQuantity(ctx.quantity,
                                       params.voronoiOpacityVarGas,
                                       params.voronoiTfLogDomain);
          voronoiTransferEditor.set_minmax(params.voronoiOpacityVarGas,
                                           tfRange.min,
                                           tfRange.max);
          if (ImGui::Button("Open opacity transfer function")) {
            voronoiTransferEditor.setComponents(MakeEditorComponents(params));
            voronoiTransferEditor.set_window();
          }
          ImGui::SameLine();
          ImGui::TextDisabled("%zu component(s)",
                              params.voronoiTfComponents.size());
          if (voronoiTransferEditor.showUI(nullptr)) {
            ApplyProjectionTransferFunction(voronoiTransferEditor, params);
            dirty = true;
          }
        }
      }
      dirty |= ImGui::Checkbox("Use GPU projection", &params.useGpuProjection);

      DrawProjectionSectionHeader("Time");
      dirty |= ImGui::InputText("Time format",
                                params.timeFormatBuf,
                                IM_ARRAYSIZE(params.timeFormatBuf));
      dirty |= ImGui::Checkbox("Use redshift", &params.flagUseRedshift);
      dirty |= ImGui::InputFloat("Time unit to display",
                                 &params.factorShownTimeInUnitTime);

      DrawProjectionSectionHeader("Zoom region");
      dirty |= ImGui::Checkbox("Rescale center to zoom-in region",
                               &params.flagSpecifyZoomRegionByMass);
	      if (params.flagSpecifyZoomRegionByMass) {
	        ImGui::SetNextItemWidth(120.0f);
	        dirty |= ImGui::InputFloat("Critical mass",
	                                   &params.criticalGasMassForZoomRegion,
	                                   0.0f,
	                                   0.0f,
	                                   "%g");
	        ImGui::SameLine();
	        ImGui::SetNextItemWidth(120.0f);
	        dirty |= ImGui::InputFloat("Zoom size",
	                                   &params.lenZoomRegion,
	                                   0.0f,
	                                   0.0f,
	                                   "%g");
	      }
	      ImGui::EndTabItem();
	    }

    if (ImGui::BeginTabItem("Overlay")) {
      DrawProjectionSectionHeader("Star particles");
      int starCount = params.starOverlayCount;
      ImGui::SetNextItemWidth(120.0f);
      if (ImGui::InputInt("Star field count", &starCount)) {
        const int oldCount = params.starOverlayCount;
        params.starOverlayCount =
          std::clamp(starCount, 1, kProjectionMaxStarOverlays);
        for (int i = oldCount; i < params.starOverlayCount; ++i) {
          ProjectionSetDefaultStarOverlayName(params, i);
        }
        dirty = true;
      }
      params.selectedStarOverlayIndex =
        std::clamp(params.selectedStarOverlayIndex,
                   0,
                   params.starOverlayCount - 1);
      std::vector<const char*> starPresetNames;
      starPresetNames.reserve(static_cast<size_t>(params.starOverlayCount));
      for (int i = 0; i < params.starOverlayCount; ++i) {
        starPresetNames.push_back(params.starOverlays[i].name);
      }
      ImGui::SetNextItemWidth(180.0f);
      dirty |= ImGui::Combo("Edit star field",
                            &params.selectedStarOverlayIndex,
                            starPresetNames.data(),
                            static_cast<int>(starPresetNames.size()));
      ProjectionStarOverlaySpec& starOverlay =
        params.starOverlays[params.selectedStarOverlayIndex];
      dirty |= ImGui::InputText("Star field name",
                                starOverlay.name,
                                IM_ARRAYSIZE(starOverlay.name));
      ImGui::SetNextItemWidth(120.0f);
      dirty |= ImGui::InputFloat("Minimum mass",
                                 &starOverlay.minMass,
                                 0.0f,
                                 0.0f,
                                 "%g");
      starOverlay.minMass = std::max(starOverlay.minMass, 0.0f);
      dirty |= ImGui::Checkbox("Auto mass range",
                               &starOverlay.autoMassRange);
      if (!starOverlay.autoMassRange ||
          starOverlay.sizeByMass ||
          starOverlay.colorByMass) {
        ImGui::SetNextItemWidth(120.0f);
        dirty |= ImGui::InputFloat("Maximum mass",
                                   &starOverlay.maxMass,
                                   0.0f,
                                   0.0f,
                                   "%g");
        starOverlay.maxMass =
          std::max(starOverlay.maxMass, starOverlay.minMass);
      }
      dirty |= ImGui::Checkbox("Size by mass", &starOverlay.sizeByMass);
      if (starOverlay.sizeByMass) {
        ImGui::SetNextItemWidth(120.0f);
        dirty |= ImGui::InputFloat("Min size (px)",
                                   &starOverlay.minSizePx,
                                   0.0f,
                                   0.0f,
                                   "%g");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        dirty |= ImGui::InputFloat("Max size (px)",
                                   &starOverlay.maxSizePx,
                                   0.0f,
                                   0.0f,
                                   "%g");
      } else {
        ImGui::SetNextItemWidth(120.0f);
        dirty |= ImGui::InputFloat("Size (px)",
                                   &starOverlay.maxSizePx,
                                   0.0f,
                                   0.0f,
                                   "%g");
      }
      starOverlay.minSizePx = std::max(starOverlay.minSizePx, 0.0f);
      starOverlay.maxSizePx =
        std::max(starOverlay.maxSizePx, starOverlay.minSizePx);
      dirty |= ImGui::ColorEdit3("Star color",
                                 starOverlay.color,
                                 ImGuiColorEditFlags_NoInputs);
      dirty |= ImGui::Checkbox("Color by mass", &starOverlay.colorByMass);

      DrawProjectionSectionHeader("Vector field");
      int vectorCount = params.vectorOverlayCount;
      ImGui::SetNextItemWidth(120.0f);
      if (ImGui::InputInt("Vector field count", &vectorCount)) {
        const int oldCount = params.vectorOverlayCount;
        params.vectorOverlayCount =
          std::clamp(vectorCount, 1, kProjectionMaxVectorOverlays);
        for (int i = oldCount; i < params.vectorOverlayCount; ++i) {
          ProjectionSetDefaultVectorOverlayName(params, i);
        }
        dirty = true;
      }
      params.selectedVectorOverlayIndex =
        std::clamp(params.selectedVectorOverlayIndex,
                   0,
                   params.vectorOverlayCount - 1);
      std::vector<const char*> vectorPresetNames;
      vectorPresetNames.reserve(static_cast<size_t>(params.vectorOverlayCount));
      for (int i = 0; i < params.vectorOverlayCount; ++i) {
        vectorPresetNames.push_back(params.vectorOverlays[i].name);
      }
      ImGui::SetNextItemWidth(180.0f);
      dirty |= ImGui::Combo("Edit vector field",
                            &params.selectedVectorOverlayIndex,
                            vectorPresetNames.data(),
                            static_cast<int>(vectorPresetNames.size()));
      ProjectionVectorOverlaySpec& overlay =
        params.vectorOverlays[static_cast<size_t>(params.selectedVectorOverlayIndex)];
      dirty |= ImGui::InputText("Vector field name",
                                overlay.name,
                                IM_ARRAYSIZE(overlay.name));
      const char* modeLabels[] = { "Arrows", "Streamlines" };
      int mode = static_cast<int>(overlay.vectorMode);
      if (ImGui::Combo("Mode", &mode, modeLabels, IM_ARRAYSIZE(modeLabels))) {
        overlay.vectorMode =
          static_cast<ProjectionVectorOverlayMode>(
            std::clamp(mode, 0, IM_ARRAYSIZE(modeLabels) - 1));
        if (overlay.vectorMode == ProjectionVectorOverlayMode::Streamlines) {
          overlay.vectorColorByMagnitude = true;
        }
        dirty = true;
      }
      const bool streamlines =
        overlay.vectorMode == ProjectionVectorOverlayMode::Streamlines;
      const char* fieldLabels[] = { "Velocity", "B field" };
      int field = static_cast<int>(overlay.vectorField);
      if (ImGui::Combo("Vector field",
                       &field,
                       fieldLabels,
                       IM_ARRAYSIZE(fieldLabels))) {
        overlay.vectorField =
          static_cast<ProjectionVectorField>(
            std::clamp(field, 0, IM_ARRAYSIZE(fieldLabels) - 1));
        dirty = true;
      }
      ImGui::SetNextItemWidth(120.0f);
      dirty |= ImGui::InputInt("Vector grid",
                               &overlay.vectorGridSize,
                               1,
                               8);
      overlay.vectorGridSize =
        std::clamp(overlay.vectorGridSize, 4, 256);
      if (!streamlines) {
        const char* scaleLabels[] = { "Linear", "Log", "Normalized" };
        int scaleMode = static_cast<int>(overlay.vectorScaleMode);
        if (ImGui::Combo("Arrow scale",
                         &scaleMode,
                         scaleLabels,
                         IM_ARRAYSIZE(scaleLabels))) {
          overlay.vectorScaleMode =
            static_cast<ProjectionVectorScaleMode>(
              std::clamp(scaleMode, 0, IM_ARRAYSIZE(scaleLabels) - 1));
          dirty = true;
        }
        dirty |= ImGui::Checkbox("Auto magnitude range",
                                 &overlay.autoMagnitudeRange);
        if (!overlay.autoMagnitudeRange) {
          ImGui::SetNextItemWidth(120.0f);
          dirty |= ImGui::InputFloat("Min magnitude",
                                     &overlay.vectorMinMagnitude,
                                     0.0f,
                                     0.0f,
                                     "%g");
          overlay.vectorMinMagnitude =
            std::max(overlay.vectorMinMagnitude, 0.0f);
          ImGui::SameLine();
          ImGui::SetNextItemWidth(120.0f);
          dirty |= ImGui::InputFloat("Max magnitude",
                                     &overlay.vectorMaxMagnitude,
                                     0.0f,
                                     0.0f,
                                     "%g");
          overlay.vectorMaxMagnitude =
            std::max(overlay.vectorMaxMagnitude, overlay.vectorMinMagnitude);
        }
        ImGui::SetNextItemWidth(120.0f);
        dirty |= ImGui::InputFloat("Min arrow size (px)",
                                   &overlay.vectorMinLengthPx,
                                   0.0f,
                                   0.0f,
                                   "%g");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        dirty |= ImGui::InputFloat("Max arrow size (px)",
                                   &overlay.vectorMaxLengthPx,
                                   0.0f,
                                   0.0f,
                                   "%g");
        overlay.vectorMinLengthPx = std::max(overlay.vectorMinLengthPx, 0.0f);
        overlay.vectorMaxLengthPx =
          std::max(overlay.vectorMaxLengthPx, overlay.vectorMinLengthPx);
      }
      ImGui::SetNextItemWidth(120.0f);
      dirty |= ImGui::SliderFloat("Opacity",
                                  &overlay.vectorOpacity,
                                  0.0f,
                                  1.0f,
                                  "%.2f");
      dirty |= ImGui::ColorEdit3("Color",
                                 overlay.vectorColor,
                                 ImGuiColorEditFlags_NoInputs);
      dirty |= ImGui::Checkbox("Color by magnitude",
                               &overlay.vectorColorByMagnitude);
      if (overlay.vectorColorByMagnitude) {
        const char* colorScaleLabels[] = { "Linear", "Log" };
        int colorScaleMode = static_cast<int>(overlay.vectorColorScaleMode);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::Combo("Color scale",
                         &colorScaleMode,
                         colorScaleLabels,
                         IM_ARRAYSIZE(colorScaleLabels))) {
          overlay.vectorColorScaleMode =
            static_cast<ProjectionVectorColorScaleMode>(
              std::clamp(colorScaleMode,
                         0,
                         IM_ARRAYSIZE(colorScaleLabels) - 1));
          dirty = true;
        }
        if (streamlines) {
          dirty |= ImGui::Checkbox("Auto color range",
                                   &overlay.autoMagnitudeRange);
        }
        if (streamlines && !overlay.autoMagnitudeRange) {
          ImGui::SetNextItemWidth(120.0f);
          dirty |= ImGui::InputFloat("Color min",
                                     &overlay.vectorMinMagnitude,
                                     0.0f,
                                     0.0f,
                                     "%g");
          overlay.vectorMinMagnitude =
            std::max(overlay.vectorMinMagnitude, 0.0f);
          ImGui::SameLine();
          ImGui::SetNextItemWidth(120.0f);
          dirty |= ImGui::InputFloat("Color max",
                                     &overlay.vectorMaxMagnitude,
                                     0.0f,
                                     0.0f,
                                     "%g");
          overlay.vectorMaxMagnitude =
            std::max(overlay.vectorMaxMagnitude, overlay.vectorMinMagnitude);
        }
        const ColormapDef* overlayColormaps = AvailableColormaps();
        const int overlayColormapCount = AvailableColormapCount();
        overlay.vectorColormapIndex =
          std::clamp(overlay.vectorColormapIndex,
                     0,
                     overlayColormapCount - 1);
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("Vector colormap",
                              overlayColormaps[overlay.vectorColormapIndex].name)) {
          for (int cmap = 0; cmap < overlayColormapCount; ++cmap) {
            const bool selected = overlay.vectorColormapIndex == cmap;
            if (ImGui::Selectable(overlayColormaps[cmap].name, selected)) {
              overlay.vectorColormapIndex = cmap;
              dirty = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
      }
      if (overlay.vectorMode == ProjectionVectorOverlayMode::Streamlines) {
        ImGui::SetNextItemWidth(120.0f);
        dirty |= ImGui::InputFloat("Streamline step (px)",
                                   &overlay.streamlineStepPx,
                                   0.0f,
                                   0.0f,
                                   "%g");
        overlay.streamlineStepPx =
          std::max(overlay.streamlineStepPx, 0.1f);
        ImGui::SetNextItemWidth(120.0f);
        dirty |= ImGui::InputInt("Max streamline steps",
                                 &overlay.streamlineMaxSteps,
                                 1,
                                 10);
        overlay.streamlineMaxSteps =
          std::max(overlay.streamlineMaxSteps, 1);
        ImGui::SetNextItemWidth(120.0f);
        dirty |= ImGui::InputInt("Streamline mask",
                                 &overlay.streamlineMaskSize,
                                 1,
                                 8);
        overlay.streamlineMaskSize =
          std::clamp(overlay.streamlineMaskSize, 8, 512);
      }
      ImGui::EndTabItem();
    }

	    ImGui::EndTabBar();
	  }

  ProjectionEnsureLayoutInitialized(params);
  ImGui::End();
  return dirty;
}

void DrawProjectionMapUI(ProjectionMapUIState& state,
                         ProjectionMapRequestState& request,
                         const ProjectionMapViewContext& ctx)
{
  if (!state.open) {
    state.selectMode = false;
    state.dragInitialized = false;
    return;
  }

  const float renderToWorld = ctx.normalization.toPhysicalScale();
  const float worldToRender = ctx.normalization.toNormalizedScale();

  if (!state.paramsInitialized ||
      state.observedToolRevision != ctx.tool.revision) {
    state.draftParams = ctx.tool.params;
    for (int i = 0; i < 3; ++i) {
      state.xlen_input[i] = state.draftParams.xlen[i] * renderToWorld;
      state.xoffset_input[i] = state.draftParams.xoffset[i] * renderToWorld;
    }
    state.paramsInitialized = true;
    state.observedToolRevision = ctx.tool.revision;
  }

  auto& params = state.draftParams;
  bool paramsDirty = false;
  ProjectionEnsureLayoutInitialized(params);

  ImGui::SetNextWindowSize(ImVec2(430, 360), ImGuiCond_Appearing);
  ImGui::Begin("make projection map", &state.open, ImGuiWindowFlags_None);
  ImGui::SetWindowFontScale(1.0f);

  paramsDirty |= ImGui::InputText("File Format",
                                  params.fileFormat,
                                  IM_ARRAYSIZE(params.fileFormat));
  paramsDirty |= ImGui::InputText("Folder",
                                  params.folderPath,
                                  IM_ARRAYSIZE(params.folderPath));

  const char* typeLabels[] = { "0", "1", "2", "3", "4", "5" };
  const int prevSelectedType = params.selectedType;
  paramsDirty |= ImGui::Combo("Selected particle type",
                              &params.selectedType,
                              typeLabels,
                              IM_ARRAYSIZE(typeLabels));
  if (prevSelectedType != params.selectedType) {
    if (params.selectedType == 0) {
      params.dataSource = DataSource::Gas;
    } else if (params.selectedType == 1 || params.selectedType == 2) {
      params.dataSource = DataSource::DM;
    } else {
      params.dataSource = DataSource::Stars;
    }
  }

  if (params.dataSource == DataSource::Stars) {
    const char* quantities[] = { "Density", "Metallicity", "Mass", "Flux" };
    int q = static_cast<int>(params.starQuantity);
    if (ImGui::Combo("Stellar quantity", &q, quantities, IM_ARRAYSIZE(quantities))) {
      params.starQuantity = static_cast<StarQuantity>(q);
      paramsDirty = true;
    }
    if (params.starQuantity == StarQuantity::Flux) {
      paramsDirty |= ImGui::InputFloat("Band center (nm)",
                                       &params.flux.band_center_nm,
                                       10.0f,
                                       100.0f,
                                       "%.1f");
      paramsDirty |= ImGui::InputFloat("Band width (nm)",
                                       &params.flux.band_width_nm,
                                       10.0f,
                                       100.0f,
                                       "%.1f");
      params.flux.band_width_nm = std::max(params.flux.band_width_nm, 1.0f);
    }
    paramsDirty |= ImGui::SliderFloat("Gaussian sigma (pixels)",
                                      &params.psf_sigma_pix,
                                      0.3f,
                                      10.0f,
                                      "%.2f");
  }

  DrawProjectionSectionHeader("Layout");
  if (ImGui::Checkbox("Multi panel", &params.multiPanelEnabled)) {
    ProjectionSyncPanelCountFromGrid(params);
    paramsDirty = true;
  }
  if (params.multiPanelEnabled) {
    const int oldPanelCount = ProjectionPanelGridCapacity(params);
    int rows = std::clamp(params.multiPanelRows, 1, 3);
    int cols = std::clamp(params.multiPanelCols, 1, 6);
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::InputInt("Rows", &rows, 1, 1)) {
      params.multiPanelRows = std::clamp(rows, 1, 3);
      ProjectionCopySeedPanels(params, oldPanelCount);
      paramsDirty = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::InputInt("Columns", &cols, 1, 1)) {
      params.multiPanelCols = std::clamp(cols, 1, 6);
      ProjectionCopySeedPanels(params, oldPanelCount);
      paramsDirty = true;
    }
    ProjectionSyncPanelCountFromGrid(params);
  }

  if (ImGui::Button("Edit layout")) {
    params.layoutEditorOpen = true;
  }

  DrawProjectionSectionHeader("Active view block");
  std::vector<const char*> blockNames;
  blockNames.reserve(static_cast<size_t>(params.viewBlockCount));
  for (int i = 0; i < params.viewBlockCount; ++i) {
    blockNames.push_back(params.viewBlocks[i].name);
  }
  params.activeViewBlockIndex =
    std::clamp(params.activeViewBlockIndex, 0, params.viewBlockCount - 1);
  if (ImGui::Combo("Active block",
                   &params.activeViewBlockIndex,
                   blockNames.data(),
                   static_cast<int>(blockNames.size()))) {
    ProjectionSyncTopLevelFromViewBlock(params, params.activeViewBlockIndex);
    paramsDirty = true;
  }
  ProjectionViewBlockSpec& activeBlock =
    params.viewBlocks[params.activeViewBlockIndex];
  paramsDirty |= ImGui::InputInt("Active block pixels",
                                 &activeBlock.npixel,
                                 10,
                                 1000);
  activeBlock.npixel = std::max(activeBlock.npixel, 1);

  paramsDirty |= ImGui::Checkbox("show cubic region", &params.flagShowCuboid);

  if (ImGui::Button("Move to camera target")) {
    request.moveCenterToCameraRequested = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Set from angular momentum")) {
    request.setAxisFromAngularMomentumRequested = true;
  }

  if (state.selectMode) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
  }
  if (ImGui::Button(state.selectMode ? "Exit Region Select" : "Select Region (Mouse Drag)")) {
    state.selectMode = !state.selectMode;
    state.dragInitialized = false;
  }
  ImGui::PopStyleColor();

  if (state.selectMode) {
    ImGuiIO& io = ImGui::GetIO();
    const float xpos = io.MousePos.x;
    const float ypos = io.MousePos.y;
    if (ImGui::IsMouseDown(0)) {
      if (!state.dragInitialized) {
        state.dragLastX = xpos;
        state.dragLastY = ypos;
        state.dragInitialized = true;
      } else if (xpos != state.dragLastX || ypos != state.dragLastY) {
        request.arcballDragRequested = true;
        request.dragOldX = state.dragLastX;
        request.dragOldY = state.dragLastY;
        request.dragNewX = xpos;
        request.dragNewY = ypos;
        request.displayWidth = io.DisplaySize.x;
        request.displayHeight = io.DisplaySize.y;
      }
      state.dragLastX = xpos;
      state.dragLastY = ypos;
    } else {
      state.dragInitialized = false;
    }
  } else {
    state.dragInitialized = false;
  }

  paramsDirty |= DrawProjectionLayoutEditor(params,
                                            ctx,
                                            renderToWorld,
                                            worldToRender);

  bool renderRequested = false;
  if (ImGui::Button("render 2D projection map")) {
    if (params.selectedType == 0) {
      params.dataSource = DataSource::Gas;
    } else if (params.selectedType == 1 || params.selectedType == 2) {
      params.dataSource = DataSource::DM;
    } else {
      params.dataSource = DataSource::Stars;
    }
    renderRequested = true;
    paramsDirty = true;
  }

  if (paramsDirty || renderRequested) {
    ProjectionEnsureLayoutInitialized(params);
    ProjectionSyncTopLevelFromViewBlock(params, params.activeViewBlockIndex);
    SubmitProjectionMapParamsRequest(params, request, renderRequested);
  }

  ImGui::End();
}


void DrawTopParticlesUI(TopParticlesUIState& state,
                        TopParticlesRequestState& request,
                        const TopParticlesResultState& result,
                        const TopParticlesViewContext& ctx) {
  if (!state.open) return;

  const QuantityState& quantity = *ctx.quantity;

  ImGui::Begin("Particles Info", &state.open);

  ImGui::InputScalar("Particle ID",
                     ImGuiDataType_S64,
                     &state.queryID);
  ImGui::SameLine();

  if (ImGui::Button("Show Info")) {
    SubmitTopParticleQueryRequest(state, request);
  }

  if (result.queryFailed && state.queryID >= 0) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1, 0, 0, 1),
                       "Particle ID %lld not found",
                       static_cast<long long>(state.queryID));
  }

  if (ImGui::Button("Refresh History")) {
    request.refreshHistoryRequested = true;
  }

  ImGui::SameLine();

  if (ImGui::Button("Clear History")) {
    request.clearHistoryRequested = true;
  }

  for (size_t i = 0; i < result.historyData.size(); i++) {
    const auto& p = result.historyData[i];
    const int64_t particleId =
      (i < result.historyIds.size()) ? result.historyIds[i] : -1;

    char label[512];
    std::snprintf(label, sizeof(label),
                  "ID %lld: mass = %.3g, pos = (%.2g, %.2g, %.2g), vel = (%.2g, %.2g, %.2g), r=%g rho=%g T=%g",
                  static_cast<long long>(particleId),
                  quantity.toDisplay(QuantityId::Mass, p.mass),
                  p.position[0], p.position[1], p.position[2],
                  p.vel[0], p.vel[1], p.vel[2],
                  quantity.toDisplay(QuantityId::Hsml, p.supportRadius),
                  quantity.toDisplay(QuantityId::Density, p.density),
                  quantity.toDisplay(QuantityId::Temperature, p.temperature));

    if (ImGui::Selectable(label, state.historySel == (int)i)) {
      state.historySel = (int)i;
      request.centerParticleId = particleId;
      request.centerParticleIndex = static_cast<size_t>(-1);
      request.centerParticleRequested = true;
    }
  }

  if (ImGui::Button("Center this paritcle")) {
    if (state.historySel >= 0 &&
        state.historySel < static_cast<int>(result.historyIds.size())) {
      request.centerParticleId = result.historyIds[state.historySel];
      request.centerParticleIndex = static_cast<size_t>(-1);
      request.followParticleRequested = true;
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Disable center paritcle")) {
    request.disableFollowParticleRequested = true;
  }

  bool flag_pushed = false;

  ImGui::Text("Select Particle Types:");
  ImGui::SameLine();
  if (ImGui::Checkbox("Type 0", &state.selectType[0])) flag_pushed = true;
  ImGui::SameLine();
  if (ImGui::Checkbox("Type 1", &state.selectType[1])) flag_pushed = true;
  ImGui::SameLine();
  if (ImGui::Checkbox("Type 2", &state.selectType[2])) flag_pushed = true;
  ImGui::SameLine();
  if (ImGui::Checkbox("Type 3", &state.selectType[3])) flag_pushed = true;
  ImGui::SameLine();
  if (ImGui::Checkbox("Type 4", &state.selectType[4])) flag_pushed = true;
  ImGui::SameLine();
  if (ImGui::Checkbox("Type 5", &state.selectType[5])) flag_pushed = true;

  ImGui::TextUnformatted("Top");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(78.0f);
  const bool countChanged = ImGui::InputInt("##top_particle_count", &state.m);
  if (state.m < 1) state.m = 1;

  bool sortChanged = false;
  const auto& catalog = quantity.catalog;
  ImGui::SameLine();
  ImGui::TextUnformatted("Sort");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(145.0f);
  if (ImGui::BeginCombo("##top_particle_sort", QuantityLabel(state.sortQuantity))) {
    for (int i = 0; i < catalog.nUIQ; ++i) {
      const QuantityId q = catalog.uiQ[i];
      const bool selected = (q == state.sortQuantity);
      if (ImGui::Selectable(QuantityLabel(q), selected)) {
        state.sortQuantity = q;
        sortChanged = true;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  sortChanged |= ImGui::Checkbox("Desc", &state.sortDescending);

  if (flag_pushed) {
    SubmitTopParticleFilterRequest(state, request);
  }
  if (sortChanged || countChanged) {
    SubmitTopParticleFilterRequest(state, request);
  }

  int count = std::min(state.m, (int)result.filtered.size());

  ImGui::Text("Selected candidates: %zu; showing top %d sorted by %s %s",
              result.filteredCandidateCount,
              count,
              QuantityLabel(result.filteredSortQuantity),
              result.filteredSortDescending ? "desc" : "asc");
  const QuantityId sortQ = result.filteredSortQuantity;
  const bool showMassColumn = sortQ != QuantityId::Mass;
  const bool showDensityColumn = sortQ != QuantityId::Density;
  const bool showTempColumn = sortQ != QuantityId::Temperature;
  int topParticleColumnCount = 5;
  if (showMassColumn) ++topParticleColumnCount;
  if (showDensityColumn) ++topParticleColumnCount;
  if (showTempColumn) ++topParticleColumnCount;

  auto textCellCenterOnClick = [&](const char* text,
                                   int64_t particleId,
                                   size_t particleIndex) {
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    if (ImGui::Selectable(text,
                          false,
                          ImGuiSelectableFlags_AllowItemOverlap,
                          ImVec2(width, 0.0f))) {
      request.centerParticleId = particleId;
      request.centerParticleIndex = particleIndex;
      request.centerParticleRequested = true;
    }
  };

  if (ImGui::BeginTable("top_particles_table",
                        topParticleColumnCount,
                        ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_Resizable |
                        ImGuiTableFlags_SizingFixedFit |
                        ImGuiTableFlags_ScrollX)) {
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 86.0f);
    ImGui::TableSetupColumn(QuantityLabel(sortQ),
                            ImGuiTableColumnFlags_WidthFixed,
                            88.0f);
    if (showMassColumn) {
      ImGui::TableSetupColumn("Mass", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    }
    if (showDensityColumn) {
      ImGui::TableSetupColumn("Density", ImGuiTableColumnFlags_WidthFixed, 82.0f);
    }
    if (showTempColumn) {
      ImGui::TableSetupColumn("Temp", ImGuiTableColumnFlags_WidthFixed, 82.0f);
    }
    ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed, 230.0f);
    ImGui::TableSetupColumn("Vel", ImGuiTableColumnFlags_WidthFixed, 230.0f);
    ImGui::TableSetupColumn("Copy", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < count; i++) {
      const int64_t particleId =
        (static_cast<size_t>(i) < result.filteredIds.size()) ? result.filteredIds[i] : -1;
      const size_t particleIndex =
        (static_cast<size_t>(i) < result.filteredIndices.size())
          ? result.filteredIndices[i]
          : static_cast<size_t>(-1);
      const float sortValue =
        (static_cast<size_t>(i) < result.filteredSortValues.size())
          ? result.filteredSortValues[i]
          : 0.0f;
      const double displaySortValue =
        quantity.toDisplay(result.filteredSortQuantity, sortValue);

      ImGui::PushID(i);
      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      char idLabel[64];
      std::snprintf(idLabel, sizeof(idLabel), "%lld",
                    static_cast<long long>(particleId));
      textCellCenterOnClick(idLabel, particleId, particleIndex);

      ImGui::TableSetColumnIndex(1);
      char sortText[64];
      std::snprintf(sortText, sizeof(sortText), "%.4g", displaySortValue);
      textCellCenterOnClick(sortText, particleId, particleIndex);

      int column = 2;
      if (showMassColumn) {
        ImGui::TableSetColumnIndex(column++);
        char massText[64];
        std::snprintf(massText,
                      sizeof(massText),
                      "%.4g",
                      quantity.toDisplay(QuantityId::Mass, result.filtered[i].mass));
        textCellCenterOnClick(massText, particleId, particleIndex);
      }
      if (showDensityColumn) {
        ImGui::TableSetColumnIndex(column++);
        char densityText[64];
        std::snprintf(densityText,
                      sizeof(densityText),
                      "%.4g",
                      quantity.toDisplay(QuantityId::Density, result.filtered[i].density));
        textCellCenterOnClick(densityText, particleId, particleIndex);
      }
      if (showTempColumn) {
        ImGui::TableSetColumnIndex(column++);
        char temperatureText[64];
        std::snprintf(temperatureText,
                      sizeof(temperatureText),
                      "%.4g",
                      quantity.toDisplay(QuantityId::Temperature, result.filtered[i].temperature));
        textCellCenterOnClick(temperatureText, particleId, particleIndex);
      }

      ImGui::TableSetColumnIndex(column++);
      char posText[128];
      std::snprintf(posText,
                    sizeof(posText),
                    "(%.3g, %.3g, %.3g)",
                    result.filtered[i].position[0],
                    result.filtered[i].position[1],
                    result.filtered[i].position[2]);
      textCellCenterOnClick(posText, particleId, particleIndex);

      ImGui::TableSetColumnIndex(column++);
      char velText[128];
      std::snprintf(velText,
                    sizeof(velText),
                    "(%.3g, %.3g, %.3g)",
                    result.filtered[i].vel[0],
                    result.filtered[i].vel[1],
                    result.filtered[i].vel[2]);
      textCellCenterOnClick(velText, particleId, particleIndex);

      ImGui::TableSetColumnIndex(column++);
      if (ImGui::SmallButton("Copy")) {
        ImGui::SetClipboardText(idLabel);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::Text("Plot 1d histogram");

  const char* quantities[] = { "x", "y", "z", "r", "Density", "Temperature", "Hsml", "Mass" };
  ImGui::Combo("Quantity", &state.selectedVar, quantities, IM_ARRAYSIZE(quantities));

  ImGui::InputInt("Number of bins", &state.bins);
  if (state.bins < 1) state.bins = 1;

  ImGui::Checkbox("Use Log scale X", &state.histogramLogScaleX);
  ImGui::Checkbox("Use Log scale Y", &state.histogramLogScaleY);

  ImGui::Checkbox("Auto Range", &state.autoRange);

  if (!state.autoRange) {
    ImGui::InputFloat("X Axis Min", &state.range1_min, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("X Axis Max", &state.range1_max, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Min", &state.range2_min, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Max", &state.range2_max, 0.0f, 0.0f, "%g");
  }

  ImGui::Checkbox("Filter: Use Camera Center", &state.useCameraCenter);
  if (state.useCameraCenter)
    ImGui::InputFloat("Camera Radius", &state.cameraRadius, 0.1f, 1.0f, "%.2f");

  if (ImGui::Button("Compute 1D Histogram")) {
    SubmitTopParticleHistogramRequest(state, request);
  }
  ImGui::Checkbox("Save plot image + JSON after compute", &state.exportHistogramPackage);
  if (state.exportFolder[0] != '\0') {
    ImGui::TextWrapped("Export folder: %s", state.exportFolder);
  }
  if (state.lastExportStatus[0] != '\0') {
    ImGui::TextWrapped("%s", state.lastExportStatus);
  }

  if (result.histogramComputed) {
    if (ImPlot::BeginPlot("Mass Histogram", ImVec2(-1, 300))) {
      if (state.histogramLogScaleY)
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
      else
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);

      ImPlot::SetupAxisLimits(ImAxis_X1, state.range1_min, state.range1_max, ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, state.range2_min, state.range2_max, ImGuiCond_Always);

      ImPlot::PlotBars("Mass",
                       result.binCenters.data(),
                       result.histBins.data(),
                       static_cast<int>(result.histBins.size()),
                       result.binSize);

      ImPlot::EndPlot();
    }
  }
  ExportTopParticleHistogramIfNeeded(state, result, ctx.exportContext);

  ImGui::End();
}

void DrawHaloesUI(HaloesUIState& state,
                  HaloesRequestState& request,
                  const HaloesViewContext& ctx)
{
#ifdef HAVE_HDF5
  if (!state.open) return;
  (void)ctx;
  
  ImGui::SetNextWindowSize(ImVec2(980, 620), ImGuiCond_Appearing);
  ImGui::Begin("Halo lists", &state.open, ImGuiWindowFlags_None);

  ImGui::InputText("Filename", state.fname, IM_ARRAYSIZE(state.fname));

  {
    if (ImGui::Button("Load halo catalog (no IDs)")) {
      SubmitHaloLoadRequest(state, request, false);
    }

    ImGui::SameLine();
    if (ImGui::Button("Load halo catalog (+ IDs)")) {
      SubmitHaloLoadRequest(state, request, true);
    }
  }

  if (!state.loaded) {
    ImGui::TextUnformatted("No haloes loaded.");
    ImGui::End();
    return;
  }

  ImGui::InputInt("Number of Halo list", &state.m);
  if (state.m < 1) state.m = 1;
  const int count = std::min(state.m, (int)state.rows.size());
  ImGui::SameLine();
  ImGui::Text(" / %zu halos", state.rows.size());

  if (!state.idsLoaded) {
    ImGui::TextUnformatted("Halo particle IDs: not loaded. (Checkbox needs IDs)");
  } else {
    ImGui::TextUnformatted("Halo particle IDs: loaded.");
    ImGui::SameLine();
    if (ImGui::Button("Clear IDs")) {
      request.clearIdsRequested = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset halo selection")) {
      state.selectedForStress.assign(state.selectedForStress.size(), 0);
      request.resetSelectionRequested = true;
      SubmitHaloStressSelectionRequest(state, request);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Recompute halo center from member particles:");

    ImGui::Checkbox("Mass-weighted", &state.recomputeUseMassWeight);
    ImGui::SameLine();
    ImGui::Checkbox("Use position (recommended)", &state.recomputeUseOriginalPos);

    ImGui::InputInt("Min N particles", &state.recomputeMinParticles);
    if (state.recomputeMinParticles < 1) state.recomputeMinParticles = 1;

    if (ImGui::Button("Recompute halo positions from particle distribution")) {
      SubmitHaloRecomputeRequest(state, request);
    }
  }

  ImGui::Separator();

  ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

  if (ImGui::BeginTable("HaloTable", 9, flags, ImVec2(0, 300))) {
    ImGui::TableSetupColumn("Stress", ImGuiTableColumnFlags_WidthFixed, 60);
    ImGui::TableSetupColumn("HaloID", ImGuiTableColumnFlags_WidthFixed, 70);
    ImGui::TableSetupColumn("Mass");
    ImGui::TableSetupColumn("Len");
    ImGui::TableSetupColumn("GasMass");
    ImGui::TableSetupColumn("StellarMass");
    ImGui::TableSetupColumn("Pos");
    ImGui::TableSetupColumn("Vel");
    ImGui::TableSetupColumn("Z(gas/star)");
    ImGui::TableHeadersRow();

    for (int i = 0; i < count; i++) {
      const HaloRowView& h = state.rows[i];

      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      bool checked = (state.idsLoaded && i < (int)state.selectedForStress.size()) ? (state.selectedForStress[i] != 0) : false;

      ImGui::PushID(i);
      if (ImGui::Checkbox("##haloStress", &checked)) {
        if (!state.idsLoaded) {
          checked = false;
        } else {
          state.selectedForStress[i] = checked ? 1 : 0;
	  SubmitHaloStressSelectionRequest(state, request);
        }
      }
      ImGui::PopID();

      ImGui::TableSetColumnIndex(1);
      ImGui::PushID(i + 100000);
      if (ImGui::Selectable(std::to_string(i).c_str(), false,
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
        request.focusHaloRequested = true;
        request.focusHaloIndex = i;
      }
      ImGui::PopID();

      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%.3g", h.groupMass);

      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%d", h.groupLen);

      ImGui::TableSetColumnIndex(4);
      ImGui::Text("%.3g", h.groupMassType[0]);

      ImGui::TableSetColumnIndex(5);
      double sm = (double)h.groupMassType[3] + h.groupMassType[4] + h.groupMassType[5];
      ImGui::Text("%.3g", (float)sm);

      ImGui::TableSetColumnIndex(6);
      ImGui::Text("(%.2g, %.2g, %.2g)", h.groupPos[0], h.groupPos[1], h.groupPos[2]);

      ImGui::TableSetColumnIndex(7);
      ImGui::Text("(%.2g, %.2g, %.2g)", h.groupVel[0], h.groupVel[1], h.groupVel[2]);

      ImGui::TableSetColumnIndex(8);
      ImGui::Text("%.3g / %.3g", h.groupMetallicity[0], h.groupMetallicity[1]);
    }

    ImGui::EndTable();
  }

  ImGui::Separator();

  ImGui::Text("Plot halo histogram");

  const char* quantities[] = {
    "Mass", "GasMass", "StellarMass", "GasMetallicity", "StellarMetallicity"
  };
  ImGui::Combo("Quantity", &state.selectedVar, quantities, IM_ARRAYSIZE(quantities));

  ImGui::InputInt("Number of bins", &state.bins);
  if (state.bins < 1) state.bins = 1;

  ImGui::Checkbox("Use Log scale X", &state.histogramLogScaleX);
  ImGui::Checkbox("Use Log scale Y", &state.histogramLogScaleY);

  ImGui::Checkbox("Auto Range", &state.autoRange);

  if (!state.autoRange) {
    ImGui::InputFloat("X Axis Min", &state.range1_min, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("X Axis Max", &state.range1_max, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Min", &state.range2_min, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Max", &state.range2_max, 0.0f, 0.0f, "%g");
  }

  if (ImGui::Button("Compute 1D Histogram")) {
    SubmitHaloHistogramRequest(state, request);
  }
  ImGui::Checkbox("Save plot image + JSON after compute", &state.exportHistogramPackage);
  if (state.exportFolder[0] != '\0') {
    ImGui::TextWrapped("Export folder: %s", state.exportFolder);
  }
  if (state.lastExportStatus[0] != '\0') {
    ImGui::TextWrapped("%s", state.lastExportStatus);
  }

  if (state.histogramComputed) {
    if (ImPlot::BeginPlot("Mass Histogram", ImVec2(-1, 300))) {
      if (state.histogramLogScaleY)
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
      else
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);

      ImPlot::SetupAxisLimits(ImAxis_X1, state.range1_min, state.range1_max, ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, state.range2_min, state.range2_max, ImGuiCond_Always);

      ImPlot::PlotBars("Mass",
                       state.binCenters.data(),
                       state.histBins.data(),
                       static_cast<int>(state.histBins.size()),
                       state.binSize);
      ImPlot::EndPlot();
    }
  }
  ExportHaloHistogramIfNeeded(state, ctx.exportContext);

  ImGui::End();
#endif
}

bool DrawMaskWindow(MaskUIState& state,
                    MaskRequestState& request,
                    ParticleMaskConfig& mask) {
  if (!state.open) return false;

  bool changed = false;
  bool apply = false;

  // Passing &ui.showWindow as the second argument allows the close button to hide it.
  if (!ImGui::Begin("Mask Settings", &state.open)) {
    ImGui::End();
    return false;
  }

  changed |= ImGui::Checkbox("Enable Sphere", &mask.enableSphere);
  changed |= ImGui::DragFloat3("Center", mask.center, 0.1f);
  changed |= ImGui::DragFloat("Radius", &mask.radius, 0.1f, 0.0f, 1e30f);

  int om = (int)mask.outsideMode;
  changed |= ImGui::Combo("Outside Mode", &om, "Drop\0Thin\0KeepAll\0");
  mask.outsideMode = (ParticleMaskConfig::OutsideMode)om;

  if (mask.outsideMode == ParticleMaskConfig::OutsideMode::Thin) {
    changed |= ImGui::DragInt("Outside Stride", &mask.outsideStride, 1.0f, 1, 1000000);
  }

  ImGui::Separator();
  ImGui::Text("Particle Type Policy");
  const char* typeNames[6] = {"Gas(0)","DM(1)","Type2","Type3","Star(4)","BH(5)"};
  for (int t=0; t<6; ++t) {
    int tm = (int)mask.typeMode[t];
    ImGui::PushID(t);
    changed |= ImGui::Combo(typeNames[t], &tm, "Off\0On (NoThin)\0On (ThinOK)\0");
    mask.typeMode[t] = (ParticleMaskConfig::TypeMode)tm;
    ImGui::PopID();
  }

  ImGui::Separator();
  changed |= ImGui::Checkbox("Enable Max Particles (ID thinning)", &mask.enableMaxParticles);
  if (mask.enableMaxParticles) {
    changed |= ImGui::DragInt("Max Particles", &mask.maxParticles, 1000.0f, 1, 1000000000);
  }

  ImGui::Separator();
  changed |= ImGui::Checkbox("Auto Apply", &state.autoApply);

  // Apply and Close buttons.
  if (ImGui::Button("Apply")) {
    request.applyRequested = true;
    apply = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Close")) {
    state.open = false; // Close explicitly.
  }

  ImGui::End();

  if (!apply && changed && state.autoApply) {
    request.applyRequested = true;
    apply = true;
  }
  return apply; // True when settings were applied.
}

void DrawProjectionPreviewUI(ProjectionMapUIState& state,
                             const ProjectionPreviewUIState& st)
{
  if (!st.valid) return;

  if (state.observedPreviewVersion != st.version) {
    state.previewOpen = true;
    state.observedPreviewVersion = st.version;
  }
  if (!state.previewOpen) return;

  if (!ImGui::Begin("2D Projection Map", &state.previewOpen, ImGuiWindowFlags_None)) {
    ImGui::End();
    return;
  }
  ImGui::Image((ImTextureID)st.textureId,
               ImVec2((float)st.width, (float)st.height));
  ImGui::End();
}
