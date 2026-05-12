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
  request.params.rmax =
    (!request.params.autorange && request.params.xmode == XAxisMode::Radius)
      ? std::max(0.0f, request.params.xmax)
      : 0.0f;
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
    if (idx < baseCount) {
      return QuantityDisplayLabel(quantity, quantity.catalog.uiQ[idx]);
    }
    return "mdot";
  };

  if (ImGui::BeginCombo("Quantity", labelAt(state.selectedVarIdx))) {
    for (int i = 0; i < baseCount; ++i) {
      bool sel = (state.selectedVarIdx == i);
      if (ImGui::Selectable(QuantityDisplayLabel(quantity,
                                                 quantity.catalog.uiQ[i]),
                            sel))
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

  params.useOriginal = true;

  ImGui::Spacing();
  const ImVec2 scaleBoxMin = ImGui::GetCursorScreenPos();
  ImGui::BeginGroup();
  ImGui::Indent(8.0f);
  ImGui::Spacing();
  ImGui::TextDisabled("Binning and scale");

  ImGui::SetNextItemWidth(72.0f);
  ImGui::InputInt("Bins", &params.bins, 0, 0);
  params.bins = std::max(1, params.bins);

  ImGui::Checkbox("Log X", &params.plotXAxisLog);
  ImGui::SameLine();
  ImGui::Checkbox("Log Y", &params.plotYAxisLog);
  ImGui::SameLine();
  ImGui::Checkbox("Auto range", &params.autorange);
  ImGui::SameLine();
  ImGui::Checkbox("Abs", &params.flagAbsolute);

  if (!params.autorange) {
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputFloat("X Min", &params.xmin, 0.0f, 0.0f, "%g");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputFloat("X Max", &params.xmax, 0.0f, 0.0f, "%g");
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputFloat("Y Min", &params.ymin, 0.0f, 0.0f, "%g");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputFloat("Y Max", &params.ymax, 0.0f, 0.0f, "%g");
  }

  ImGui::Spacing();
  ImGui::Unindent(8.0f);
  ImGui::EndGroup();
  const ImVec2 scaleBoxMax = ImGui::GetItemRectMax();
  ImGui::GetWindowDrawList()->AddRect(scaleBoxMin,
                                      scaleBoxMax,
                                      ImGui::GetColorU32(ImGuiCol_Border),
                                      4.0f);
  ImGui::Spacing();

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
      const char* ylabel = params.isMDot
        ? "mdot"
        : QuantityDisplayLabel(quantity, params.var1);
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
  const QuantityState& quantity = ctx.quantity;
  const QuantityCatalogState& catalog = quantity.catalog;
  auto& params = state.draftParams;

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
  ImGui::Begin("histogram 2D", &state.open, ImGuiWindowFlags_None);

  const char* typeLabels[] = {
    "Type 0", "Type 1", "Type 2", "Type 3", "Type 4", "Type 5"
  };
  params.particleType = std::clamp(params.particleType, 0, 5);
  int firstNonEmptyType = -1;
  for (int t = 0; t < 6; ++t) {
    if (ctx.particleTypeCounts[static_cast<size_t>(t)] > 0) {
      firstNonEmptyType = t;
      break;
    }
  }
  const bool hasAnyParticles = firstNonEmptyType >= 0;
  if (hasAnyParticles &&
      ctx.particleTypeCounts[static_cast<size_t>(params.particleType)] == 0) {
    params.particleType = firstNonEmptyType;
  }

  ImGui::SetNextItemWidth(120.0f);
  if (!hasAnyParticles) {
    ImGui::BeginDisabled();
  }
  const char* currentTypeLabel = hasAnyParticles
    ? typeLabels[params.particleType]
    : "No particles";
  if (ImGui::BeginCombo("Particle type", currentTypeLabel)) {
    for (int t = 0; t < 6; ++t) {
      const size_t count = ctx.particleTypeCounts[static_cast<size_t>(t)];
      const bool enabled = count > 0;
      if (!enabled) {
        ImGui::BeginDisabled();
      }
      char label[64];
      std::snprintf(label, sizeof(label), "%s (%zu)", typeLabels[t], count);
      const bool selected = params.particleType == t;
      if (ImGui::Selectable(label, selected) && enabled) {
        params.particleType = t;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
      if (!enabled) {
        ImGui::EndDisabled();
      }
    }
    ImGui::EndCombo();
  }
  if (!hasAnyParticles) {
    ImGui::EndDisabled();
  }
  ImGui::SameLine();
  ImGui::Checkbox("Scatter plot", &params.showScatter);
  if (params.showScatter) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(86.0f);
    ImGui::InputInt("Max points", &params.scatterMaxPoints, 0, 0);
    params.scatterMaxPoints = std::clamp(params.scatterMaxPoints, 1, 10000);
  }

  auto quantityAvailableForSelectedType = [&](QuantityId quantity) {
    if (!QuantityShowInUI(quantity)) {
      return true;
    }
    const int type = std::clamp(params.particleType, 0, 5);
    for (int q = 0; q < catalog.nUIQByType[static_cast<size_t>(type)]; ++q) {
      if (catalog.uiQByType[static_cast<size_t>(type)][q] == quantity) {
        return true;
      }
    }
    return false;
  };

  auto firstAvailableQuantity = [&]() {
    for (int q = 0; q < catalog.nAllQ; ++q) {
      const QuantityId cand = catalog.allQ[q];
      if (quantityAvailableForSelectedType(cand)) {
        return cand;
      }
    }
    return QuantityId::Mass;
  };

  if (!quantityAvailableForSelectedType(params.var1)) {
    params.var1 = firstAvailableQuantity();
  }
  if (!quantityAvailableForSelectedType(params.var2)) {
    params.var2 = firstAvailableQuantity();
  }

  auto drawQuantityCombo = [&](const char* label, QuantityId& quantity) {
    if (ImGui::BeginCombo(label, QuantityDisplayLabel(ctx.quantity, quantity))) {
      for (int q = 0; q < catalog.nAllQ; ++q) {
        QuantityId cand = catalog.allQ[q];
        if (!quantityAvailableForSelectedType(cand)) {
          continue;
        }
        const bool isSelected = cand == quantity;
        if (ImGui::Selectable(QuantityDisplayLabel(ctx.quantity, cand),
                              isSelected)) {
          quantity = cand;
        }
        if (isSelected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  };

  drawQuantityCombo("X Axis Quantity", params.var1);
  drawQuantityCombo("Y Axis Quantity", params.var2);

  ImGui::Spacing();
  const ImVec2 scaleBoxMin = ImGui::GetCursorScreenPos();
  ImGui::BeginGroup();
  ImGui::Indent(8.0f);
  ImGui::Spacing();
  ImGui::TextDisabled("Binning and scale");
  if (!params.showScatter) {
    ImGui::SetNextItemWidth(72.0f);
    ImGui::InputInt("Bins X", &params.bins1, 0, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.0f);
    ImGui::InputInt("Bins Y", &params.bins2, 0, 0);
  }
  ImGui::Checkbox("Log X", &params.logScaleX);
  ImGui::SameLine();
  ImGui::Checkbox("Log Y", &params.logScaleY);
  if (!params.showScatter) {
    ImGui::SameLine();
    ImGui::Checkbox("Log color", &params.logScaleColor);
  }
  ImGui::SameLine();
  ImGui::Checkbox("Auto range", &params.autoRange);
  if (!params.autoRange) {
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputFloat("X Min", &params.range1_min, 0.0f, 0.0f, "%g");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputFloat("X Max", &params.range1_max, 0.0f, 0.0f, "%g");
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputFloat("Y Min", &params.range2_min, 0.0f, 0.0f, "%g");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputFloat("Y Max", &params.range2_max, 0.0f, 0.0f, "%g");
  }
  ImGui::Spacing();
  ImGui::Unindent(8.0f);
  ImGui::EndGroup();
  const ImVec2 scaleBoxMax = ImGui::GetItemRectMax();
  ImGui::GetWindowDrawList()->AddRect(scaleBoxMin,
                                      scaleBoxMax,
                                      ImGui::GetColorU32(ImGuiCol_Border),
                                      4.0f);
  ImGui::Spacing();

#ifdef USE_CONVEX_HULL
  ImGui::Checkbox("Filter: Use Convex Hull", &params.useConvexHull);
#endif

  ImGui::Checkbox("Filter: Use Camera Center", &params.useCameraCenter);
  if (params.useCameraCenter) {
    ImGui::InputFloat("Camera Radius", &params.cameraRadius, 0.1f, 1.0f, "%.2f");
  }

  if (!hasAnyParticles) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button(params.showScatter ? "Compute Scatter" : "Compute Histogram")) {
    SubmitHistogram2DRequest(state, request);
  }
  if (!hasAnyParticles) {
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("No particles are loaded.");
  }
  ImGui::Checkbox("Save plot image + JSON after compute", &state.exportPlotPackage);
  if (state.exportFolder[0] != '\0') {
    ImGui::TextWrapped("Export folder: %s", state.exportFolder);
  }
  if (state.lastExportStatus[0] != '\0') {
    ImGui::TextWrapped("%s", state.lastExportStatus);
  }

  if (result.computed && result.result.valid) {
    const Histogram2DParams& plotParams = result.paramsUsed;
    if (ImPlot::BeginPlot(plotParams.showScatter ? "Scatter Plot" : "2D Histogram",
                          ImVec2(-1, 300))) {
      ImPlot::SetupAxes(QuantityDisplayLabel(quantity, plotParams.var1),
                         QuantityDisplayLabel(quantity, plotParams.var2));

      ImPlot::SetupAxisLimits(ImAxis_X1,
                              result.result.range1_min,
                              result.result.range1_max,
                              ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1,
                              result.result.range2_min,
                              result.result.range2_max,
                              ImGuiCond_Always);

      if (plotParams.showScatter) {
        ImPlot::PlotScatter("Particles",
                            result.result.scatterX.data(),
                            result.result.scatterY.data(),
                            static_cast<int>(result.result.scatterX.size()));
      } else {
        const size_t computedBins1 = result.result.values.size();
        const size_t computedBins2 =
          (computedBins1 > 0) ? result.result.values[0].size() : 0;
        std::vector<float> heatmapData;
        heatmapData.reserve(computedBins1 * computedBins2);
        for (size_t j = 0; j < computedBins2; j++) {
          for (size_t i = 0; i < computedBins1; i++) {
            heatmapData.push_back(result.result.values[i][j]);
          }
        }

        ImPlot::PushColormap(ImPlotColormap_Viridis);
        ImPlot::PlotHeatmap("Histogram",
                            heatmapData.data(),
                            static_cast<int>(computedBins2),
                            static_cast<int>(computedBins1),
                            0,
                            0,
                            "",
                            ImPlotPoint(result.result.range1_min,
                                        result.result.range2_min),
                            ImPlotPoint(result.result.range1_max,
                                        result.result.range2_max));
        ImPlot::PopColormap();
      }

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
                                 const QuantityState& quantityState)
{
  bool dirty = false;
  const QuantityCatalogState& catalog = quantityState.catalog;
  if (ImGui::BeginCombo(label, QuantityDisplayLabel(quantityState, quantity))) {
    for (int q = 0; q < catalog.nUIQ; ++q) {
      QuantityId cand = catalog.uiQ[q];
      bool isSelected = (cand == quantity);
      if (ImGui::Selectable(QuantityDisplayLabel(quantityState, cand),
                            isSelected)) {
        quantity = cand;
        dirty = true;
      }
      if (isSelected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return dirty;
}

bool DrawProjectionSignedAxisCombo(const char* label, int& selectedAxis, int& sign)
{
  const char* axisLabels[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
  selectedAxis = std::clamp(selectedAxis, 0, 2);
  int value = selectedAxis * 2 + (sign < 0 ? 1 : 0);
  const bool changed =
    ImGui::Combo(label, &value, axisLabels, IM_ARRAYSIZE(axisLabels));
  if (changed) {
    selectedAxis = std::clamp(value / 2, 0, 2);
    sign = (value % 2) == 1 ? -1 : 1;
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

template <typename Body>
void DrawProjectionSubsectionBox(const char* label, Body body)
{
  ImGui::Spacing();
  const ImVec2 boxMin = ImGui::GetCursorScreenPos();
  ImGui::BeginGroup();
  ImGui::Indent(8.0f);
  ImGui::Spacing();
  ImGui::TextDisabled("%s", label);
  body();
  ImGui::Spacing();
  ImGui::Unindent(8.0f);
  ImGui::EndGroup();
  const ImVec2 boxMax = ImGui::GetItemRectMax();
  ImGui::GetWindowDrawList()->AddRect(boxMin,
                                      boxMax,
                                      ImGui::GetColorU32(ImGuiCol_Border),
                                      4.0f);
}

void DrawProjectionLargeLabel(const char* label)
{
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
                                ProjectionMapUIState& ui,
                                const ProjectionMapViewContext& ctx,
                                float renderToWorld)
{
  static TransferFunctionEditor voronoiTransferEditor;
  bool dirty = false;
  if (!ui.layoutEditorOpen) return false;

  ProjectionEnsureLayoutInitialized(params);

  ImGui::SetNextWindowSize(ImVec2(1080, 520), ImGuiCond_Appearing);
  ImGui::Begin("Projection layout editor", &ui.layoutEditorOpen);
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

      const ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_SizingFixedFit;
      if (ImGui::BeginTable("projection_panels_table",
                            7,
                            tableFlags,
                            ImVec2(0.0f, 210.0f))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34.0f);
        ImGui::TableSetupColumn("Block", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Quantity", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Stars", ImGuiTableColumnFlags_WidthFixed, 115.0f);
        ImGui::TableSetupColumn("Vector", ImGuiTableColumnFlags_WidthFixed, 115.0f);
        ImGui::TableSetupColumn("Labels", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        const ColormapDef* colormaps = AvailableColormaps();
        const int colormapCount = AvailableColormapCount();
        for (int i = 0; i < panelCount; ++i) {
          ProjectionPanelSpec& panel = params.panels[i];
          panel.viewBlockIndex =
            std::clamp(panel.viewBlockIndex, 0, params.viewBlockCount - 1);
          panel.colormapIndex =
            std::clamp(panel.colormapIndex, 0, colormapCount - 1);
          ImGui::PushID(i);
          ImGui::TableNextRow();
          if (ui.selectedPanelIndex == i) {
            ImGui::TableSetBgColor(
              ImGuiTableBgTarget_RowBg0,
              ImGui::GetColorU32(ImGuiCol_Header));
          }
          ImGui::TableSetColumnIndex(0);
          char panelLabel[32];
          std::snprintf(panelLabel, sizeof(panelLabel), "%d", i + 1);
          if (ImGui::Selectable(panelLabel,
                                ui.selectedPanelIndex == i,
                                0,
                                ImVec2(24.0f, 0.0f))) {
            ui.selectedPanelIndex = i;
            params.activeViewBlockIndex = panel.viewBlockIndex;
            ui.selectedViewBlockIndex = panel.viewBlockIndex;
            ProjectionSyncTopLevelFromViewBlock(params, panel.viewBlockIndex);
            dirty = true;
          }

          const bool rowClicked = ImGui::IsItemClicked();

          ImGui::TableSetColumnIndex(1);
          ImGui::TextUnformatted(blockNames[panel.viewBlockIndex]);
          ImGui::TableSetColumnIndex(2);
          ImGui::TextUnformatted(QuantityDisplayLabel(ctx.quantity,
                                                      panel.quantity));
          ImGui::TableSetColumnIndex(3);
          ImGui::Text("%s%s%s",
                      colormaps[panel.colormapIndex].name,
                      panel.flagLogScale ? " log" : "",
                      panel.autoRange ? " auto" : " manual");

          auto overlayLabel = [](int index,
                                 const char* prefix,
                                 int count) -> std::string {
            if (index <= 0) return "Off";
            return std::string(prefix) + " " +
                   std::to_string(std::clamp(index, 1, count));
          };

          ImGui::TableSetColumnIndex(4);
          const std::string starLabel =
            overlayLabel(panel.starOverlayIndex,
                         "Star field",
                         params.starOverlayCount);
          ImGui::TextUnformatted(starLabel.c_str());

          ImGui::TableSetColumnIndex(5);
          const std::string vectorLabel =
            overlayLabel(panel.vectorOverlayIndex,
                         "Vector field",
                         params.vectorOverlayCount);
          ImGui::TextUnformatted(vectorLabel.c_str());

          ImGui::TableSetColumnIndex(6);
          const bool showTime = ProjectionResolveLabelMode(
            panel.timeLabelMode,
            true);
          const bool showScale = ProjectionResolveLabelMode(
            panel.scaleBarMode,
            false);
          ImGui::Text("%s%s%s",
                      showTime ? "Time" : "",
                      (showTime && showScale) ? " / " : "",
                      showScale ? "Scale" : "");
          if (rowClicked) {
            ui.selectedPanelIndex = i;
            params.activeViewBlockIndex = panel.viewBlockIndex;
            ui.selectedViewBlockIndex = panel.viewBlockIndex;
            ProjectionSyncTopLevelFromViewBlock(params, panel.viewBlockIndex);
            dirty = true;
          }
          ImGui::PopID();
        }
        ImGui::EndTable();
      }

      ui.selectedPanelIndex =
        std::clamp(ui.selectedPanelIndex, 0, panelCount - 1);
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("View blocks")) {
      ImGui::SetNextItemWidth(92.0f);
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
        const int src = std::clamp(ui.selectedViewBlockIndex,
                                   0,
                                   params.viewBlockCount - 1);
        const int dst = params.viewBlockCount++;
        params.viewBlocks[dst] = params.viewBlocks[src];
        ProjectionSetDefaultViewBlockName(params, dst);
        params.viewBlocks[dst].sizeSameAsMain = true;
        params.viewBlocks[dst].centerSameAsMain = true;
        params.viewBlocks[dst].tiltSameAsMain = true;
        ui.selectedViewBlockIndex = dst;
        params.activeViewBlockIndex = dst;
        dirty = true;
      }

      ui.selectedViewBlockIndex =
        std::clamp(ui.selectedViewBlockIndex, 0, params.viewBlockCount - 1);
      std::vector<const char*> viewBlockNames;
      viewBlockNames.reserve(static_cast<size_t>(params.viewBlockCount));
      for (int i = 0; i < params.viewBlockCount; ++i) {
        viewBlockNames.push_back(params.viewBlocks[i].name);
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(170.0f);
      if (ImGui::Combo("Edit block",
                       &ui.selectedViewBlockIndex,
                       viewBlockNames.data(),
                       static_cast<int>(viewBlockNames.size()))) {
        params.activeViewBlockIndex = ui.selectedViewBlockIndex;
        ProjectionSyncTopLevelFromViewBlock(params,
                                           ui.selectedViewBlockIndex);
        dirty = true;
      }
      ProjectionViewBlockSpec& block =
        params.viewBlocks[ui.selectedViewBlockIndex];
      ImGui::Separator();
      DrawProjectionLargeLabel(block.name);
      const bool isMainBlock = ui.selectedViewBlockIndex == 0;

      DrawProjectionSubsectionBox("Block", [&]() {
        if (ImGui::BeginTable("ViewBlockIdentityGrid", 4)) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::SetNextItemWidth(180.0f);
          dirty |= ImGui::InputText("Name##view_block",
                                    block.name,
                                    IM_ARRAYSIZE(block.name));
          ImGui::TableSetColumnIndex(1);
          ImGui::SetNextItemWidth(96.0f);
          dirty |= ImGui::InputInt("Pixels##view_block",
                                   &block.npixel,
                                   0,
                                   0);
          block.npixel = std::max(block.npixel, 1);
          if (!isMainBlock) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            dirty |= ImGui::Checkbox("Center same as main",
                                     &block.centerSameAsMain);
            ImGui::TableSetColumnIndex(1);
            dirty |= ImGui::Checkbox("Size same as main",
                                     &block.sizeSameAsMain);
            ImGui::TableSetColumnIndex(2);
            dirty |= ImGui::Checkbox("Tilt same as main",
                                     &block.tiltSameAsMain);
          }
          ImGui::EndTable();
        }
      });

      const bool showRegionSize = isMainBlock || !block.sizeSameAsMain;
      const bool showRegionCenter = isMainBlock || !block.centerSameAsMain;
      if (showRegionSize || showRegionCenter) {
        DrawProjectionSubsectionBox("Region", [&]() {
      if (showRegionSize) {
        float xlenOriginal[3] = {
          block.xlen[0],
          block.xlen[1],
          block.xlen[2]
        };
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::InputFloat3("Size", xlenOriginal)) {
          for (int k = 0; k < 3; ++k) block.xlen[k] = xlenOriginal[k];
          dirty = true;
        }
      }

      if (showRegionCenter) {
        float xoffsetOriginal[3] = {
          block.xoffset[0],
          block.xoffset[1],
          block.xoffset[2]
        };
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::InputFloat3("Center", xoffsetOriginal)) {
          for (int k = 0; k < 3; ++k) block.xoffset[k] = xoffsetOriginal[k];
          dirty = true;
        }
      }
      if (showRegionCenter) {
        if (ImGui::Button("Set center from camera")) {
          const glm::vec3 dataTarget =
            ctx.camera.cameraTarget * renderToWorld;
          block.xoffset[0] = dataTarget.x;
          block.xoffset[1] = dataTarget.y;
          block.xoffset[2] = dataTarget.z;
          dirty = true;
        }
      }
        });
      }

      if (isMainBlock || !block.tiltSameAsMain) {
        DrawProjectionSubsectionBox("Orientation", [&]() {
        ImGui::SetNextItemWidth(150.0f);
        dirty |= DrawProjectionSignedAxisCombo("Projection normal",
                                               block.selectedAxis,
                                               block.projectionSign);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        dirty |= DrawProjectionSignedAxisCombo("Up direction",
                                               block.upAxis,
                                               block.upSign);
        ImGui::SetNextItemWidth(260.0f);
        dirty |= ImGui::InputFloat3("Tilt (deg)", block.tilt);
        });
      }
      DrawProjectionSubsectionBox("Scale bar", [&]() {
        ImGui::SetNextItemWidth(96.0f);
        dirty |= ImGui::InputFloat("Length / box",
                                   &block.scaleBarFractionDefault,
                                   0.0f,
                                   0.0f,
                                   "%g");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        dirty |= ImGui::InputText("Arrow label",
                                  block.arrowLabelStrDefault,
                                  IM_ARRAYSIZE(block.arrowLabelStrDefault));
      });
      if (dirty && ui.selectedViewBlockIndex == params.activeViewBlockIndex) {
        ProjectionSyncTopLevelFromViewBlock(params, params.activeViewBlockIndex);
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Rendering")) {
      ImGui::TextDisabled("Projection");
      if (ImGui::BeginTable("ProjectionMethodGrid", 5)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const char* methodLabels[] = { "Standard", "Voronoi" };
        int method = params.flagVoronoi ? 1 : 0;
        ImGui::SetNextItemWidth(116.0f);
        if (ImGui::Combo("Method##projection_method",
                         &method,
                         methodLabels,
                         IM_ARRAYSIZE(methodLabels))) {
          params.flagVoronoi = method == 1;
          dirty = true;
        }

        ImGui::TableSetColumnIndex(1);
        int voronoiMode = static_cast<int>(params.voronoiMode);
        const char* modeLabels[] = { "Weighted mean", "Opacity rendering" };
        ImGui::BeginDisabled(!params.flagVoronoi);
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::Combo("Mode##projection_method",
                         &voronoiMode,
                         modeLabels,
                         IM_ARRAYSIZE(modeLabels))) {
          params.voronoiMode =
            static_cast<ProjectionVoronoiMode>(voronoiMode);
          dirty = true;
        }
        ImGui::EndDisabled();

        ImGui::TableSetColumnIndex(2);
        ImGui::BeginDisabled(!params.flagVoronoi);
        ImGui::SetNextItemWidth(86.0f);
        dirty |= ImGui::InputInt("Slices##projection_method",
                                 &params.step_z,
                                 10,
                                 1000);
        ImGui::EndDisabled();

        ImGui::TableSetColumnIndex(3);
        dirty |= ImGui::Checkbox("Density weight##projection_method",
                                 &params.flagDensityWeight);

        ImGui::TableSetColumnIndex(4);
        dirty |= ImGui::Checkbox("GPU##projection_method",
                                 &params.useGpuProjection);
        ImGui::EndTable();
      }

      if (params.flagVoronoi) {
        if (params.voronoiMode == ProjectionVoronoiMode::OpacityRendering) {
          DrawProjectionSubsectionBox("Opacity rendering", [&]() {
          dirty |= DrawProjectionQuantityCombo("Opacity quantity",
                                               params.voronoiOpacityVarGas,
                                               ctx.quantity);
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
          });
        }
      }

      DrawProjectionSubsectionBox("Time", [&]() {
        if (ImGui::BeginTable("ProjectionTimeGrid", 3)) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::SetNextItemWidth(140.0f);
          dirty |= ImGui::InputText("Format##projection_time",
                                    params.timeFormatBuf,
                                    IM_ARRAYSIZE(params.timeFormatBuf));
          ImGui::TableSetColumnIndex(1);
          dirty |= ImGui::Checkbox("Use redshift##projection_time",
                                   &params.flagUseRedshift);
          ImGui::TableSetColumnIndex(2);
          ImGui::SetNextItemWidth(96.0f);
          dirty |= ImGui::InputFloat("Unit##projection_time",
                                     &params.factorShownTimeInUnitTime,
                                     0.0f,
                                     0.0f,
                                     "%g");
          ImGui::EndTable();
        }
      });

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
      ImGui::SetNextItemWidth(92.0f);
      if (ImGui::InputInt("Star field count", &starCount)) {
        const int oldCount = params.starOverlayCount;
        params.starOverlayCount =
          std::clamp(starCount, 1, kProjectionMaxStarOverlays);
        for (int i = oldCount; i < params.starOverlayCount; ++i) {
          ProjectionSetDefaultStarOverlayName(params, i);
        }
        dirty = true;
      }
      ui.selectedStarOverlayIndex =
        std::clamp(ui.selectedStarOverlayIndex,
                   0,
                   params.starOverlayCount - 1);
      std::vector<const char*> starPresetNames;
      starPresetNames.reserve(static_cast<size_t>(params.starOverlayCount));
      for (int i = 0; i < params.starOverlayCount; ++i) {
        starPresetNames.push_back(params.starOverlays[i].name);
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(170.0f);
      dirty |= ImGui::Combo("Edit star field",
                            &ui.selectedStarOverlayIndex,
                            starPresetNames.data(),
                            static_cast<int>(starPresetNames.size()));
      ProjectionStarOverlaySpec& starOverlay =
        params.starOverlays[ui.selectedStarOverlayIndex];
      ImGui::Separator();
      DrawProjectionLargeLabel(starOverlay.name);
      const char* scalarLabels[] = {
        "Fixed",
        "Mass",
        "Luminosity",
        "Density",
        "Metallicity",
        "Temperature"
      };
      const char* sizeScaleLabels[] = {
        "Fixed",
        "Linear",
        "Sqrt",
        "Log",
        "Saturating"
      };

      DrawProjectionSubsectionBox("Star field", [&]() {
        ImGui::SetNextItemWidth(220.0f);
        dirty |= ImGui::InputText("Name",
                                  starOverlay.name,
                                  IM_ARRAYSIZE(starOverlay.name));
        dirty |= ImGui::Checkbox("Type 3", &starOverlay.typeEnabled[3]);
        ImGui::SameLine();
        dirty |= ImGui::Checkbox("Type 4", &starOverlay.typeEnabled[4]);
        ImGui::SameLine();
        dirty |= ImGui::Checkbox("Type 5", &starOverlay.typeEnabled[5]);

        if (ImGui::BeginTable("StarMassFilterGrid", 4)) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::SetNextItemWidth(96.0f);
          dirty |= ImGui::InputFloat("Min mass",
                                     &starOverlay.minMass,
                                     0.0f,
                                     0.0f,
                                     "%g");
          starOverlay.minMass = std::max(starOverlay.minMass, 0.0f);

          ImGui::TableSetColumnIndex(1);
          dirty |= ImGui::Checkbox("Use max##star_mass",
                                   &starOverlay.useMaxMass);
          if (starOverlay.useMaxMass) {
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(96.0f);
            dirty |= ImGui::InputFloat("Max mass",
                                       &starOverlay.maxMass,
                                       0.0f,
                                       0.0f,
                                       "%g");
            starOverlay.maxMass =
              std::max(starOverlay.maxMass, starOverlay.minMass);
          }
          ImGui::EndTable();
        }
      });

      const bool variableSize =
        starOverlay.sizeScalar != ProjectionParticleOverlayScalar::Fixed &&
        starOverlay.sizeScale != ProjectionParticleSizeScale::Fixed;
      DrawProjectionSubsectionBox("Size", [&]() {
        if (ImGui::BeginTable("StarSizeGrid", 4)) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          int sizeScalar = static_cast<int>(starOverlay.sizeScalar);
          ImGui::SetNextItemWidth(132.0f);
          if (ImGui::Combo("Value##star_size",
                           &sizeScalar,
                           scalarLabels,
                           IM_ARRAYSIZE(scalarLabels))) {
            const auto oldScalar = starOverlay.sizeScalar;
            starOverlay.sizeScalar =
              static_cast<ProjectionParticleOverlayScalar>(sizeScalar);
            if (oldScalar == ProjectionParticleOverlayScalar::Fixed &&
                starOverlay.sizeScalar != ProjectionParticleOverlayScalar::Fixed &&
                starOverlay.sizeScale == ProjectionParticleSizeScale::Fixed) {
              starOverlay.sizeScale = ProjectionParticleSizeScale::Log;
            }
            dirty = true;
          }

          if (variableSize) {
            ImGui::TableSetColumnIndex(1);
            int sizeScale = static_cast<int>(starOverlay.sizeScale);
            ImGui::SetNextItemWidth(116.0f);
            if (ImGui::Combo("Scale##star_size",
                             &sizeScale,
                             sizeScaleLabels,
                             IM_ARRAYSIZE(sizeScaleLabels))) {
              starOverlay.sizeScale =
                static_cast<ProjectionParticleSizeScale>(sizeScale);
              dirty = true;
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(76.0f);
            dirty |= ImGui::InputFloat("Min px##star_size",
                                       &starOverlay.minSizePx,
                                       0.0f,
                                       0.0f,
                                       "%g");
            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(76.0f);
            dirty |= ImGui::InputFloat("Max px##star_size",
                                       &starOverlay.maxSizePx,
                                       0.0f,
                                       0.0f,
                                       "%g");
            starOverlay.minSizePx = std::max(starOverlay.minSizePx, 0.0f);
            starOverlay.maxSizePx =
              std::max(starOverlay.maxSizePx, starOverlay.minSizePx);
          } else {
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(86.0f);
            dirty |= ImGui::InputFloat("Size px##star_size",
                                       &starOverlay.maxSizePx,
                                       0.0f,
                                       0.0f,
                                       "%g");
            starOverlay.maxSizePx = std::max(starOverlay.maxSizePx, 0.0f);
          }

          if (variableSize) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            dirty |= ImGui::Checkbox("Auto range##star_size",
                                     &starOverlay.autoSizeRange);
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(76.0f);
            dirty |= ImGui::InputInt("Bins##star_size", &starOverlay.sizeBins);
            starOverlay.sizeBins = std::clamp(starOverlay.sizeBins, 0, 64);
          }

          if (variableSize && !starOverlay.autoSizeRange) {
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(96.0f);
            dirty |= ImGui::InputFloat("Range min##star_size",
                                       &starOverlay.sizeValueMin,
                                       0.0f,
                                       0.0f,
                                       "%g");
            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(96.0f);
            dirty |= ImGui::InputFloat("Range max##star_size",
                                       &starOverlay.sizeValueMax,
                                       0.0f,
                                       0.0f,
                                       "%g");
            starOverlay.sizeValueMax =
              std::max(starOverlay.sizeValueMax, starOverlay.sizeValueMin);
          }

          ImGui::EndTable();
        }
      });

      DrawProjectionSubsectionBox("Color and opacity", [&]() {
        const ColormapDef* starColormaps = AvailableColormaps();
        const int starColormapCount = AvailableColormapCount();
        starOverlay.colorColormapIndex =
          std::clamp(starOverlay.colorColormapIndex,
                     0,
                     starColormapCount - 1);
        if (ImGui::BeginTable("StarColorGrid", 4)) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          int colorScalar = static_cast<int>(starOverlay.colorScalar);
          ImGui::SetNextItemWidth(132.0f);
          if (ImGui::Combo("Value##star_color",
                           &colorScalar,
                           scalarLabels,
                           IM_ARRAYSIZE(scalarLabels))) {
            starOverlay.colorScalar =
              static_cast<ProjectionParticleOverlayScalar>(colorScalar);
            dirty = true;
          }

          ImGui::TableSetColumnIndex(1);
          ImGui::SetNextItemWidth(120.0f);
          dirty |= ImGui::SliderFloat("Opacity##star_color",
                                      &starOverlay.opacity,
                                      0.0f,
                                      1.0f,
                                      "%.2f");

          if (starOverlay.colorScalar ==
              ProjectionParticleOverlayScalar::Fixed) {
            ImGui::TableSetColumnIndex(2);
            dirty |= ImGui::ColorEdit3("Fixed color##star_color",
                                       starOverlay.color,
                                       ImGuiColorEditFlags_NoInputs);
            ImGui::TableSetColumnIndex(3);
          } else {
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(132.0f);
            if (ImGui::BeginCombo("Color scale##star_color",
                                  starColormaps[starOverlay.colorColormapIndex].name)) {
              for (int cmap = 0; cmap < starColormapCount; ++cmap) {
                const bool selected = starOverlay.colorColormapIndex == cmap;
                if (ImGui::Selectable(starColormaps[cmap].name, selected)) {
                  starOverlay.colorColormapIndex = cmap;
                  dirty = true;
                }
                if (selected) {
                  ImGui::SetItemDefaultFocus();
                }
              }
              ImGui::EndCombo();
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            dirty |= ImGui::Checkbox("Auto range##star_color",
                                     &starOverlay.autoColorRange);
            ImGui::TableSetColumnIndex(1);
            dirty |= ImGui::Checkbox("Log color##star_color",
                                     &starOverlay.colorLogScale);

            if (!starOverlay.autoColorRange) {
              ImGui::TableSetColumnIndex(2);
              ImGui::SetNextItemWidth(96.0f);
              dirty |= ImGui::InputFloat("Min##star_color",
                                         &starOverlay.colorValueMin,
                                         0.0f,
                                         0.0f,
                                         "%g");
              ImGui::TableSetColumnIndex(3);
              ImGui::SetNextItemWidth(96.0f);
              dirty |= ImGui::InputFloat("Max##star_color",
                                         &starOverlay.colorValueMax,
                                         0.0f,
                                         0.0f,
                                         "%g");
              starOverlay.colorValueMax =
                std::max(starOverlay.colorValueMax, starOverlay.colorValueMin);
            }
          }

          ImGui::TableSetColumnIndex(3);
          const char* symbolLabels[] = {
            "Soft circle",
            "Asterisk",
            "Filled circle",
            "Ring",
            "Star",
            "Plus",
            "Cross",
            "Diamond",
            "Square"
          };
          int symbol = static_cast<int>(starOverlay.symbol);
          ImGui::SetNextItemWidth(116.0f);
          if (ImGui::Combo("Symbol##star_color",
                           &symbol,
                           symbolLabels,
                           IM_ARRAYSIZE(symbolLabels))) {
            starOverlay.symbol =
              static_cast<ProjectionParticleSymbol>(
                std::clamp(symbol, 0, IM_ARRAYSIZE(symbolLabels) - 1));
            dirty = true;
          }

          ImGui::EndTable();
        }
      });

      DrawProjectionSectionHeader("Vector field");
      int vectorCount = params.vectorOverlayCount;
      ImGui::SetNextItemWidth(92.0f);
      if (ImGui::InputInt("Vector field count", &vectorCount)) {
        const int oldCount = params.vectorOverlayCount;
        params.vectorOverlayCount =
          std::clamp(vectorCount, 1, kProjectionMaxVectorOverlays);
        for (int i = oldCount; i < params.vectorOverlayCount; ++i) {
          ProjectionSetDefaultVectorOverlayName(params, i);
        }
        dirty = true;
      }
      ui.selectedVectorOverlayIndex =
        std::clamp(ui.selectedVectorOverlayIndex,
                   0,
                   params.vectorOverlayCount - 1);
      std::vector<const char*> vectorPresetNames;
      vectorPresetNames.reserve(static_cast<size_t>(params.vectorOverlayCount));
      for (int i = 0; i < params.vectorOverlayCount; ++i) {
        vectorPresetNames.push_back(params.vectorOverlays[i].name);
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(170.0f);
      dirty |= ImGui::Combo("Edit vector field",
                            &ui.selectedVectorOverlayIndex,
                            vectorPresetNames.data(),
                            static_cast<int>(vectorPresetNames.size()));
      ProjectionVectorOverlaySpec& overlay =
        params.vectorOverlays[static_cast<size_t>(ui.selectedVectorOverlayIndex)];
      ImGui::Separator();
      DrawProjectionLargeLabel(overlay.name);
      const char* modeLabels[] = { "Arrows", "Streamlines" };
      const char* fieldLabels[] = { "Velocity", "B field" };
      bool streamlines =
        overlay.vectorMode == ProjectionVectorOverlayMode::Streamlines;

      DrawProjectionSubsectionBox("Vector field", [&]() {
        ImGui::SetNextItemWidth(220.0f);
        dirty |= ImGui::InputText("Name##vector_field",
                                  overlay.name,
                                  IM_ARRAYSIZE(overlay.name));
        if (ImGui::BeginTable("VectorFieldGrid", 4)) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          int mode = static_cast<int>(overlay.vectorMode);
          ImGui::SetNextItemWidth(132.0f);
          if (ImGui::Combo("Mode##vector_field",
                           &mode,
                           modeLabels,
                           IM_ARRAYSIZE(modeLabels))) {
            overlay.vectorMode =
              static_cast<ProjectionVectorOverlayMode>(
                std::clamp(mode, 0, IM_ARRAYSIZE(modeLabels) - 1));
            if (overlay.vectorMode == ProjectionVectorOverlayMode::Streamlines) {
              overlay.vectorColorByMagnitude = true;
            }
            dirty = true;
          }
          ImGui::TableSetColumnIndex(1);
          int field = static_cast<int>(overlay.vectorField);
          ImGui::SetNextItemWidth(132.0f);
          if (ImGui::Combo("Field##vector_field",
                           &field,
                           fieldLabels,
                           IM_ARRAYSIZE(fieldLabels))) {
            overlay.vectorField =
              static_cast<ProjectionVectorField>(
                std::clamp(field, 0, IM_ARRAYSIZE(fieldLabels) - 1));
            dirty = true;
          }
          ImGui::TableSetColumnIndex(2);
          ImGui::SetNextItemWidth(76.0f);
          dirty |= ImGui::InputInt("Grid##vector_field",
                                   &overlay.vectorGridSize,
                                   1,
                                   8);
          overlay.vectorGridSize =
            std::clamp(overlay.vectorGridSize, 4, 256);
          ImGui::EndTable();
        }
      });

      streamlines =
        overlay.vectorMode == ProjectionVectorOverlayMode::Streamlines;

      if (!streamlines) {
        DrawProjectionSubsectionBox("Arrow size", [&]() {
          const char* scaleLabels[] = { "Linear", "Log", "Normalized" };
          if (ImGui::BeginTable("VectorArrowGrid", 4)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            int scaleMode = static_cast<int>(overlay.vectorScaleMode);
            ImGui::SetNextItemWidth(132.0f);
            if (ImGui::Combo("Scale##vector_arrow",
                             &scaleMode,
                             scaleLabels,
                             IM_ARRAYSIZE(scaleLabels))) {
              overlay.vectorScaleMode =
                static_cast<ProjectionVectorScaleMode>(
                  std::clamp(scaleMode, 0, IM_ARRAYSIZE(scaleLabels) - 1));
              dirty = true;
            }
            ImGui::TableSetColumnIndex(1);
            dirty |= ImGui::Checkbox("Auto range##vector_arrow",
                                     &overlay.autoMagnitudeRange);
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(76.0f);
            dirty |= ImGui::InputFloat("Min px##vector_arrow",
                                       &overlay.vectorMinLengthPx,
                                       0.0f,
                                       0.0f,
                                       "%g");
            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(76.0f);
            dirty |= ImGui::InputFloat("Max px##vector_arrow",
                                       &overlay.vectorMaxLengthPx,
                                       0.0f,
                                       0.0f,
                                       "%g");
            overlay.vectorMinLengthPx =
              std::max(overlay.vectorMinLengthPx, 0.0f);
            overlay.vectorMaxLengthPx =
              std::max(overlay.vectorMaxLengthPx, overlay.vectorMinLengthPx);

            if (!overlay.autoMagnitudeRange) {
              ImGui::TableNextRow();
              ImGui::TableSetColumnIndex(2);
              ImGui::SetNextItemWidth(96.0f);
              dirty |= ImGui::InputFloat("Mag min##vector_arrow",
                                         &overlay.vectorMinMagnitude,
                                         0.0f,
                                         0.0f,
                                         "%g");
              overlay.vectorMinMagnitude =
                std::max(overlay.vectorMinMagnitude, 0.0f);
              ImGui::TableSetColumnIndex(3);
              ImGui::SetNextItemWidth(96.0f);
              dirty |= ImGui::InputFloat("Mag max##vector_arrow",
                                         &overlay.vectorMaxMagnitude,
                                         0.0f,
                                         0.0f,
                                         "%g");
              overlay.vectorMaxMagnitude =
                std::max(overlay.vectorMaxMagnitude,
                         overlay.vectorMinMagnitude);
            }
            ImGui::EndTable();
          }
        });
      }

      DrawProjectionSubsectionBox("Color and opacity", [&]() {
        const ColormapDef* overlayColormaps = AvailableColormaps();
        const int overlayColormapCount = AvailableColormapCount();
        overlay.vectorColormapIndex =
          std::clamp(overlay.vectorColormapIndex,
                     0,
                     overlayColormapCount - 1);
        if (ImGui::BeginTable("VectorColorGrid", 4)) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          dirty |= ImGui::Checkbox("By magnitude##vector_color",
                                   &overlay.vectorColorByMagnitude);
          ImGui::TableSetColumnIndex(1);
          ImGui::SetNextItemWidth(120.0f);
          dirty |= ImGui::SliderFloat("Opacity##vector_color",
                                      &overlay.vectorOpacity,
                                      0.0f,
                                      1.0f,
                                      "%.2f");
          if (overlay.vectorColorByMagnitude) {
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(132.0f);
            if (ImGui::BeginCombo("Color scale##vector_color",
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
          } else {
            ImGui::TableSetColumnIndex(2);
            dirty |= ImGui::ColorEdit3("Color##vector_color",
                                       overlay.vectorColor,
                                       ImGuiColorEditFlags_NoInputs);
          }

          if (overlay.vectorColorByMagnitude) {
            const char* colorScaleLabels[] = { "Linear", "Log" };
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            int colorScaleMode = static_cast<int>(overlay.vectorColorScaleMode);
            ImGui::SetNextItemWidth(116.0f);
            if (ImGui::Combo("Scale##vector_color",
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
              ImGui::TableSetColumnIndex(1);
              dirty |= ImGui::Checkbox("Auto range##vector_color",
                                       &overlay.autoMagnitudeRange);
            }
            if (streamlines && !overlay.autoMagnitudeRange) {
              ImGui::TableSetColumnIndex(2);
              ImGui::SetNextItemWidth(96.0f);
              dirty |= ImGui::InputFloat("Min##vector_color",
                                         &overlay.vectorMinMagnitude,
                                         0.0f,
                                         0.0f,
                                         "%g");
              overlay.vectorMinMagnitude =
                std::max(overlay.vectorMinMagnitude, 0.0f);
              ImGui::TableSetColumnIndex(3);
              ImGui::SetNextItemWidth(96.0f);
              dirty |= ImGui::InputFloat("Max##vector_color",
                                         &overlay.vectorMaxMagnitude,
                                         0.0f,
                                         0.0f,
                                         "%g");
              overlay.vectorMaxMagnitude =
                std::max(overlay.vectorMaxMagnitude,
                         overlay.vectorMinMagnitude);
            }
          }
          ImGui::EndTable();
        }
      });

      if (overlay.vectorMode == ProjectionVectorOverlayMode::Streamlines) {
        DrawProjectionSubsectionBox("Streamlines", [&]() {
          if (ImGui::BeginTable("VectorStreamlineGrid", 3)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(96.0f);
            dirty |= ImGui::InputFloat("Step px##streamline",
                                       &overlay.streamlineStepPx,
                                       0.0f,
                                       0.0f,
                                       "%g");
            overlay.streamlineStepPx =
              std::max(overlay.streamlineStepPx, 0.1f);
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(96.0f);
            dirty |= ImGui::InputInt("Max steps##streamline",
                                     &overlay.streamlineMaxSteps,
                                     1,
                                     10);
            overlay.streamlineMaxSteps =
              std::max(overlay.streamlineMaxSteps, 1);
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(96.0f);
            dirty |= ImGui::InputInt("Mask##streamline",
                                     &overlay.streamlineMaskSize,
                                     1,
                                     8);
            overlay.streamlineMaskSize =
              std::clamp(overlay.streamlineMaskSize, 8, 512);
            ImGui::EndTable();
          }
        });
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

  if (!state.paramsInitialized ||
      state.observedToolRevision != ctx.tool.revision) {
    state.draftParams = ctx.tool.params;
    for (int i = 0; i < 3; ++i) {
      state.xlen_input[i] = state.draftParams.xlen[i];
      state.xoffset_input[i] = state.draftParams.xoffset[i];
    }
    state.paramsInitialized = true;
    state.observedToolRevision = ctx.tool.revision;
  }

  auto& params = state.draftParams;
  bool paramsDirty = false;
  ProjectionEnsureLayoutInitialized(params);

  ImGui::SetNextWindowSize(ImVec2(640, 660), ImGuiCond_Appearing);
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
    state.layoutEditorOpen = true;
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
    state.selectedViewBlockIndex = params.activeViewBlockIndex;
    ProjectionSyncTopLevelFromViewBlock(params, params.activeViewBlockIndex);
    paramsDirty = true;
  }
  ProjectionViewBlockSpec& activeBlock =
    params.viewBlocks[params.activeViewBlockIndex];

  DrawProjectionSubsectionBox("Region", [&]() {
    float center[3] = {
      activeBlock.xoffset[0],
      activeBlock.xoffset[1],
      activeBlock.xoffset[2]
    };
    float cubeSize = activeBlock.xlen[0];

    if (ImGui::BeginTable("MainActiveViewBlockGrid", 3)) {
      ImGui::TableSetupColumn("CenterColumn",
                              ImGuiTableColumnFlags_WidthFixed,
                              280.0f);
      ImGui::TableSetupColumn("SizeColumn",
                              ImGuiTableColumnFlags_WidthFixed,
                              130.0f);
      ImGui::TableSetupColumn("PixelsColumn",
                              ImGuiTableColumnFlags_WidthFixed,
                              120.0f);
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::SetNextItemWidth(210.0f);
      if (ImGui::InputFloat3("Center##main_active_view_block", center)) {
        for (int k = 0; k < 3; ++k) {
          activeBlock.xoffset[k] = center[k];
        }
        paramsDirty = true;
      }

      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(70.0f);
      if (ImGui::InputFloat("Size##main_active_view_block",
                            &cubeSize,
                            0.0f,
                            0.0f,
                            "%g")) {
        cubeSize = std::max(cubeSize, 0.0f);
        for (int k = 0; k < 3; ++k) {
          activeBlock.xlen[k] = cubeSize;
        }
        paramsDirty = true;
      }

      ImGui::TableSetColumnIndex(2);
      const float pixelsWidth =
        ImGui::CalcTextSize("00000").x +
        ImGui::GetStyle().FramePadding.x * 2.0f;
      ImGui::SetNextItemWidth(pixelsWidth);
      paramsDirty |= ImGui::InputInt("Pixels##main_active_view_block",
                                     &activeBlock.npixel,
                                     0,
                                     0);
      activeBlock.npixel = std::max(activeBlock.npixel, 1);
      ImGui::EndTable();
    }

    paramsDirty |= ImGui::Checkbox("Show cubic region##main_active_view_block",
                                   &params.flagShowCuboid);
    ImGui::Spacing();

    if (ImGui::Button("Set center from camera##main_active_view_block")) {
      const glm::vec3 dataTarget =
        ctx.camera.cameraTarget * renderToWorld;
      activeBlock.xoffset[0] = dataTarget.x;
      activeBlock.xoffset[1] = dataTarget.y;
      activeBlock.xoffset[2] = dataTarget.z;
      paramsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Set from angular momentum##main_active_view_block")) {
      request.setAxisFromAngularMomentumRequested = true;
    }
    ImGui::SameLine();
    if (state.selectMode) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    } else {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
    }
    if (ImGui::Button(state.selectMode
                        ? "Exit Region Select##main_active_view_block"
                        : "Select Region##main_active_view_block")) {
      state.selectMode = !state.selectMode;
      state.dragInitialized = false;
    }
    ImGui::PopStyleColor();
  });

  ImGui::Separator();

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
                                            state,
                                            ctx,
                                            renderToWorld);

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
  if (ImGui::BeginCombo("##top_particle_sort",
                        QuantityDisplayLabel(quantity, state.sortQuantity))) {
    for (int i = 0; i < catalog.nUIQ; ++i) {
      const QuantityId q = catalog.uiQ[i];
      const bool selected = (q == state.sortQuantity);
      if (ImGui::Selectable(QuantityDisplayLabel(quantity, q), selected)) {
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
              QuantityDisplayLabel(quantity, result.filteredSortQuantity),
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
    ImGui::TableSetupColumn(QuantityDisplayLabel(quantity, sortQ),
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
