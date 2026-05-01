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
#include "data/simulation_dataset.h"
#include "data/halo_store.h"

#include "projection/make_2D_projection_map.h"
#include "projection/projection_map_params.h"
#include "projection/projection_geometry.h"
#include "analysis/radial_profile.h"
#include "analysis/histogram2d.h"
#include "analysis/profile_histogram_export.h"

#include <algorithm>

extern void UpdateCuboidTransformArcball(CuboidObject& cuboid,
                                         float oldX, float oldY,
                                         float newX, float newY,
					 float screenWidth, float screenHeight,
                                         const glm::mat4& view,
                                         const glm::vec3& pivot);

namespace {
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

void DrawProjectionMapUI(ProjectionMapUIState& state,
                         ProjectionMapRequestState& request,
                         const ProjectionMapViewContext& ctx)
{
  if (!state.open) {
    state.selectMode = false;
    state.dragInitialized = false;
    return;
  }

  WindowCommandQueue& windowCommands = ctx.windowCommands;
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
  
  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
  ImGui::Begin("make projectoin map", &state.open, ImGuiWindowFlags_None);

  if (ImGui::InputFloat3("side length (original)", state.xlen_input)) {
    params.xlen[0] = state.xlen_input[0] * worldToRender;
    params.xlen[1] = state.xlen_input[1] * worldToRender;
    params.xlen[2] = state.xlen_input[2] * worldToRender;
    paramsDirty = true;
  }

  if (ImGui::InputFloat3("offset center (original)", state.xoffset_input)) {
    params.xoffset[0] = state.xoffset_input[0] * worldToRender;
    params.xoffset[1] = state.xoffset_input[1] * worldToRender;
    params.xoffset[2] = state.xoffset_input[2] * worldToRender;
    paramsDirty = true;
  }
  
  paramsDirty |= ImGui::InputInt("npixel", &params.npixel, 10, 1000);
  paramsDirty |= ImGui::InputFloat3("Plane Tilt (deg) x", params.tilt);

  if (ImGui::Button("move center to camera target")) {
    request.moveCenterToCameraRequested = true;
  }

  paramsDirty |= ImGui::Checkbox("show cubic region", &params.flagShowCuboid);

  // -----------------------------
  // mouse drag region selection
  // -----------------------------
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

  // -----------------------------
  // angular momentum axis
  // -----------------------------
  if (ImGui::Button("set axis from angular momentum")) {
    request.setAxisFromAngularMomentumRequested = true;
  }
  
  // -----------------------------
  // output file
  // -----------------------------
  paramsDirty |= ImGui::InputText("File Format",
                                  params.fileFormat,
                                  IM_ARRAYSIZE(params.fileFormat));
  paramsDirty |= ImGui::InputText("Folder",
                                  params.folderPath,
                                  IM_ARRAYSIZE(params.folderPath));

  // -----------------------------
  // projection axis
  // -----------------------------
  const char* axisLabels[] = {
    "X-Axis (YZ Plane)",
    "Y-Axis (XZ Plane)",
    "Z-Axis (XY Plane)"
  };
  paramsDirty |= ImGui::Combo("Projection Normal Axis",
                              &params.selectedAxis,
                              axisLabels,
                              IM_ARRAYSIZE(axisLabels));

  // -----------------------------
  // particle type
  // -----------------------------
  const char* typeLabels[] = { "0", "1", "2", "3", "4", "5" };
  const int prevSelectedType = params.selectedType;
  paramsDirty |= ImGui::Combo("SelectedParticleType",
                              &params.selectedType,
                              typeLabels,
                              IM_ARRAYSIZE(typeLabels));

  // Infer dataSource naturally from type.
  if (prevSelectedType != params.selectedType) {
    if (params.selectedType == 0) {
      params.dataSource = DataSource::Gas;
    } else if (params.selectedType == 1 || params.selectedType == 2) {
      params.dataSource = DataSource::DM;
    } else {
      params.dataSource = DataSource::Stars;
    }
  }

  // -----------------------------
  // colormap
  // -----------------------------
  const ColormapDef* colormaps = AvailableColormaps();
  const int colormapCount = AvailableColormapCount();
  if (params.colormapindex < 0) params.colormapindex = 0;
  if (params.colormapindex >= colormapCount) params.colormapindex = colormapCount - 1;
  
  const char* previewName = colormaps[params.colormapindex].name;
  if (ImGui::BeginCombo("Colormap##projection", previewName)) {
    for (int i = 0; i < colormapCount; ++i) {
      bool selected = (params.colormapindex == i);
      if (ImGui::Selectable(colormaps[i].name, selected)) {
	params.colormapindex = i;
        paramsDirty = true;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  
  // -----------------------------
  // data source specific UI
  // -----------------------------
  if (params.dataSource == DataSource::Gas) {
    if (ImGui::BeginCombo("Quantity", QuantityLabel(params.selectedVarGas))) {
      for (int q = 0; q < static_cast<int>(state.gasQuantityOptions.size()); ++q) {
        QuantityId cand = state.gasQuantityOptions[q];
        bool is_selected = (cand == params.selectedVarGas);
        if (ImGui::Selectable(QuantityLabel(cand), is_selected)) {
          params.selectedVarGas = cand;
          params.var = QuantityLabel(cand);
          paramsDirty = true;
        }
        if (is_selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    if (ImGui::Checkbox("Multi quantity projection grid",
                        &params.multiPanelEnabled)) {
      if (params.multiPanelEnabled) {
        params.dataSource = DataSource::Gas;
        params.multiPanelCount = std::clamp(params.multiPanelCount, 1, 6);
      }
      paramsDirty = true;
    }

    if (params.multiPanelEnabled) {
      ImGui::Indent();
      int rows = std::clamp(params.multiPanelRows, 1, 3);
      int cols = std::clamp(params.multiPanelCols, 1, 6);

      ImGui::SetNextItemWidth(80.0f);
      if (ImGui::InputInt("Rows", &rows, 1, 1)) {
        rows = std::clamp(rows, 1, 3);
        params.multiPanelRows = rows;
        params.multiPanelCount = std::min(rows * params.multiPanelCols, 6);
        paramsDirty = true;
      }

      ImGui::SetNextItemWidth(80.0f);
      ImGui::SameLine();
      if (ImGui::InputInt("Columns", &cols, 1, 1)) {
        cols = std::clamp(cols, 1, 6);
        params.multiPanelCols = cols;
        params.multiPanelCount = std::min(params.multiPanelRows * cols, 6);
        paramsDirty = true;
      }

      params.multiPanelRows = std::clamp(params.multiPanelRows, 1, 3);
      params.multiPanelCols = std::clamp(params.multiPanelCols, 1, 6);
      params.multiPanelCount =
        std::min(params.multiPanelRows * params.multiPanelCols, 6);

      const int panelCount =
        std::clamp(params.multiPanelCount,
                   1,
                   std::min(params.multiPanelRows * params.multiPanelCols, 6));
      for (int panel = 0; panel < panelCount; ++panel) {
        char label[32];
        std::snprintf(label, sizeof(label), "Panel %d", panel + 1);
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::BeginCombo(label, QuantityLabel(params.multiPanelVars[panel]))) {
          for (int q = 0; q < static_cast<int>(state.gasQuantityOptions.size()); ++q) {
            QuantityId cand = state.gasQuantityOptions[q];
            bool is_selected = (cand == params.multiPanelVars[panel]);
            if (ImGui::Selectable(QuantityLabel(cand), is_selected)) {
              params.multiPanelVars[panel] = cand;
              paramsDirty = true;
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }

        char timeId[32];
        std::snprintf(timeId, sizeof(timeId), "Time##panel%d", panel);
        ImGui::SameLine();
        ImGui::BeginDisabled(!params.flagTimeLabel);
        paramsDirty |= ImGui::Checkbox(timeId,
                                       &params.multiPanelShowTimeLabel[panel]);
        ImGui::EndDisabled();

        char scaleId[32];
        std::snprintf(scaleId, sizeof(scaleId), "Scale##panel%d", panel);
        ImGui::SameLine();
        ImGui::BeginDisabled(!params.flagPlaceScale);
        paramsDirty |= ImGui::Checkbox(scaleId,
                                       &params.multiPanelShowScale[panel]);
        ImGui::EndDisabled();
      }
      ImGui::Unindent();
    }

    paramsDirty |= ImGui::Checkbox("Density Weighting", &params.flagDensityWeight);
    paramsDirty |= ImGui::Checkbox("Use Voronoi tesselation", &params.flagVoronoi);

    if (params.flagVoronoi) {
      ImGui::SetNextItemWidth(200.0f);
      ImGui::SameLine();
      paramsDirty |= ImGui::InputInt("nz", &params.step_z, 10, 1000);
    }
  }
  else if (params.dataSource == DataSource::Stars) {
    if (params.flagDensityWeight || params.flagVoronoi) {
      params.flagDensityWeight = false;
      params.flagVoronoi = false;
      paramsDirty = true;
    }

    const char* quantities[] = { "Density", "Metallicity", "Mass", "Flux" };
    int q = static_cast<int>(params.starQuantity);
    if (ImGui::Combo("Quantity", &q, quantities, IM_ARRAYSIZE(quantities))) {
      params.starQuantity = static_cast<StarQuantity>(q);
      paramsDirty = true;

      switch (params.starQuantity) {
      case StarQuantity::Density:
	params.var = "stellar density";
	break;
      case StarQuantity::Metallicity:
	params.var = "stellar metallicity";
	break;
      case StarQuantity::Mass:
	params.var = "stellar mass";
	break;
      case StarQuantity::Flux:
	params.var = "stellar flux";
	break;
      }
    }

    if (params.starQuantity == StarQuantity::Flux) {
      ImGui::SeparatorText("Flux Settings");
      paramsDirty |= ImGui::InputFloat("Band center (nm)",
                                       &params.flux.band_center_nm,
                                       10.0f, 100.0f, "%.1f");
      paramsDirty |= ImGui::InputFloat("Band width  (nm)",
                                       &params.flux.band_width_nm,
                                       10.0f, 100.0f, "%.1f");
      params.flux.band_width_nm = std::max(params.flux.band_width_nm, 1.0f);
    }

    paramsDirty |= ImGui::SliderFloat("Gaussian sigma (pixels)",
                                      &params.psf_sigma_pix,
                                      0.3f, 10.0f, "%.2f");
  }
  else {
    // DM
    if (params.flagDensityWeight || params.flagVoronoi || params.var != "DM projection") {
      params.flagDensityWeight = false;
      params.flagVoronoi = false;
      params.var = "DM projection";
      paramsDirty = true;
    }
  }

  // -----------------------------
  // color scale / range
  // -----------------------------
  paramsDirty |= ImGui::Checkbox("Use Log color scale", &params.flagLogScale);
  paramsDirty |= ImGui::Checkbox("Auto Range", &params.autoRange);

  if (!params.autoRange) {
    ImGui::Indent();
    ImGui::SetNextItemWidth(100.0f);
    paramsDirty |= ImGui::InputFloat("Min", &params.range_min, 0.0f, 0.0f, "%g");

    ImGui::SetNextItemWidth(100.0f);
    ImGui::SameLine();
    paramsDirty |= ImGui::InputFloat("Max", &params.range_max, 0.0f, 0.0f, "%g");
    ImGui::Unindent();
  }

  // -----------------------------
  // time label
  // -----------------------------
  paramsDirty |= ImGui::Checkbox("Show time label", &params.flagTimeLabel);
  if (params.flagTimeLabel) {
    ImGui::Indent();
    paramsDirty |= ImGui::InputText("Time Format",
                                    params.timeFormatBuf,
                                    IM_ARRAYSIZE(params.timeFormatBuf));

    ImGui::SameLine();
    paramsDirty |= ImGui::Checkbox("Use redshift", &params.flagUseRedshift);

    paramsDirty |= ImGui::InputFloat("Time unit to display", &params.factorShownTimeInUnitTime);

    ImGui::Unindent();
  }

  // -----------------------------
  // scale bar
  // -----------------------------
  paramsDirty |= ImGui::Checkbox("Show spatial scale", &params.flagPlaceScale);
  if (params.flagPlaceScale) {
    ImGui::Indent();
    ImGui::SetNextItemWidth(80);
    paramsDirty |= ImGui::InputFloat("arrow size (original)", &params.arrowLenX);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    paramsDirty |= ImGui::InputText("arrow label",
                                    params.arrowLabelStr,
                                    sizeof(params.arrowLabelStr));

    ImGui::Unindent();
  }

  // -----------------------------
  // zoom region
  // -----------------------------
  paramsDirty |= ImGui::Checkbox("Rescale center to zoom-in region",
                                 &params.flagSpecifyZoomRegionByMass);
  if (params.flagSpecifyZoomRegionByMass) {
    ImGui::Indent();
    ImGui::SetNextItemWidth(80);
    paramsDirty |= ImGui::InputFloat("critical mass",
                                     &params.criticalGasMassForZoomRegion,
                                     0.0f, 0.0f, "%g");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    paramsDirty |= ImGui::InputFloat("size of zoom-in region (original)",
                                     &params.lenZoomRegion,
                                     0.0f, 0.0f, "%g");
    ImGui::Unindent();
  }

  // -----------------------------
  // font selection
  // -----------------------------
  if (ImGui::Button("Select font")) {
    windowCommands.open(WindowId::ProjectionFontSelection);
  }

  // -----------------------------
  // star particle overlay
  // -----------------------------
  paramsDirty |= ImGui::Checkbox("draw star particle", &params.flagShowStarParticles);

  if (params.flagShowStarParticles) {
    paramsDirty |= ImGui::InputText("Filter Expression",
                                    params.filterExpr,
                                    IM_ARRAYSIZE(params.filterExpr));
    paramsDirty |= ImGui::InputText("Point Size Expression",
                                    params.pointSizeExpr,
                                    IM_ARRAYSIZE(params.pointSizeExpr));
    paramsDirty |= ImGui::InputText("Point Color Expression",
                                    params.pointColorExpr,
                                    IM_ARRAYSIZE(params.pointColorExpr));
    paramsDirty |= ImGui::InputText("Min Value Expression",
                                    params.minValueExpr,
                                    IM_ARRAYSIZE(params.minValueExpr));
    paramsDirty |= ImGui::InputText("Max Value Expression",
                                    params.maxValueExpr,
                                    IM_ARRAYSIZE(params.maxValueExpr));
  }

  // -----------------------------
  // render
  // -----------------------------
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
                  p.supportRadius, p.density, p.temperature);

    if (ImGui::Selectable(label, state.historySel == (int)i)) {
      state.historySel = (int)i;
      request.centerParticleId = particleId;
      request.centerParticleRequested = true;
    }
  }

  if (ImGui::Button("Center this paritcle")) {
    if (state.historySel >= 0 &&
        state.historySel < static_cast<int>(result.historyIds.size())) {
      request.centerParticleId = result.historyIds[state.historySel];
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

  ImGui::InputInt("Number of Particles", &state.m);
  if (state.m < 1) state.m = 1;

  if (flag_pushed) {
    SubmitTopParticleFilterRequest(state, request);
  }

  int count = std::min(state.m, (int)result.filtered.size());

  ImGui::Text("Type %d : Showing top %d particles sorted by mass", state.particleType, count);
  for (int i = 0; i < count; i++) {
    const int64_t particleId =
      (static_cast<size_t>(i) < result.filteredIds.size()) ? result.filteredIds[i] : -1;
    char label[512];
    std::snprintf(label, sizeof(label),
                  "ID %lld: mass = %.3g, pos = (%.2g, %.2g, %.2g) vel = (%.2g, %.2g, %.2g), radius = %g rho=%g t=%g",
                  static_cast<long long>(particleId),
                  quantity.toDisplay(QuantityId::Mass, result.filtered[i].mass),
                  result.filtered[i].position[0], result.filtered[i].position[1], result.filtered[i].position[2],
                  result.filtered[i].vel[0], result.filtered[i].vel[1], result.filtered[i].vel[2],
                  result.filtered[i].supportRadius,
                  result.filtered[i].density, result.filtered[i].temperature);

    if (ImGui::Selectable(label)) {
      request.centerParticleId = particleId;
      request.centerParticleRequested = true;
    }
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
