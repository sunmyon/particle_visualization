#include "app/state/clump_window_state.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <cstdio>
#include <glm/vec3.hpp>
#include <imgui.h>
#include "implot.h"

#include "analysis/profile_histogram_export.h"
#include "app/state/plot_export_state.h"

namespace {
constexpr const char* kClumpFinderQuantities[] = {
  "Density", "Temperature", "Hsml"
};
constexpr int kNumClumpFinderQuantities =
  static_cast<int>(sizeof(kClumpFinderQuantities) / sizeof(kClumpFinderQuantities[0]));

std::string MakeExportTimestamp()
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

std::filesystem::path EnsureExportFolder(char* folder, size_t folderSize)
{
  if (folder && folder[0] != '\0') {
    return std::filesystem::path(folder);
  }
  const std::filesystem::path dir =
    std::filesystem::temp_directory_path() /
    ("particle_vis_implot_exports_" + MakeExportTimestamp());
  if (folder && folderSize > 0) {
    std::snprintf(folder, folderSize, "%s", dir.string().c_str());
  }
  return dir;
}

AnalysisPlotExportSpec MakeSpec(const PlotBatchExportViewContext& ctx,
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

void StoreStatus(char* status, size_t statusSize, const AnalysisPlotExportResult& result)
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

void BuildHistogramBins(const std::vector<float>& samples,
                        int bins,
                        float xmin,
                        float xmax,
                        std::vector<float>& centers,
                        std::vector<float>& counts,
                        float& binSize)
{
  bins = std::max(1, bins);
  binSize = (xmax - xmin) / static_cast<float>(bins);
  if (binSize <= 0.0f) binSize = 1.0f;
  centers.resize(bins);
  counts.assign(static_cast<size_t>(bins), 0.0f);
  for (int i = 0; i < bins; ++i) {
    centers[static_cast<size_t>(i)] = xmin + (static_cast<float>(i) + 0.5f) * binSize;
  }
  for (float v : samples) {
    int bin = static_cast<int>((v - xmin) / binSize);
    if (bin < 0) bin = 0;
    if (bin >= bins) bin = bins - 1;
    counts[static_cast<size_t>(bin)] += 1.0f;
  }
}

void ExportClumpFinderHistogramIfNeeded(ClumpFinderWindowState& ui,
                                        const PlotBatchExportViewContext& ctx)
{
  if (!ui.exportHistogramPackage ||
      !ui.histogramComputed ||
      ui.histogramVersion == ui.lastExportedHistogramVersion) {
    return;
  }

  std::vector<float> centers;
  std::vector<float> counts;
  float binSize = 1.0f;
  BuildHistogramBins(ui.massHistogramValues,
                     ui.histogramBinsAuto ? std::max(1, ui.histogramBins) : ui.histogramBins,
                     ui.histogramRangeMin,
                     ui.histogramRangeMax,
                     centers,
                     counts,
                     binSize);

  float yMax = 1.0f;
  for (float v : counts) yMax = std::max(yMax, v);

  BarHistogramPlotExportParams params;
  params.kind = "clump_finder_mass_histogram";
  params.title = "Clump Mass Histogram";
  params.xLabel = ui.histogramLogScaleX ? "log10(Mass)" : "Mass";
  params.yLabel = "Count";
  params.logX = false;
  params.logY = ui.histogramLogScaleY;
  params.xMin = ui.histogramRangeMin;
  params.xMax = ui.histogramRangeMax;
  params.yMin = ui.histogramLogScaleY ? 0.8f : 0.0f;
  params.yMax = yMax;
  params.binSize = binSize;

  const std::filesystem::path dir =
    EnsureExportFolder(ui.exportFolder, sizeof(ui.exportFolder));
  const std::string stem =
    "clump_finder_histogram_" +
    std::to_string(static_cast<unsigned long long>(ui.histogramVersion));
  AnalysisPlotExportSpec spec = MakeSpec(ctx, dir, stem);
  AnalysisPlotExportResult result =
    ExportBarHistogramPlotPackage(spec, params, centers, counts);
  StoreStatus(ui.lastExportStatus, sizeof(ui.lastExportStatus), result);
  if (result.ok) {
    ui.lastExportedHistogramVersion = ui.histogramVersion;
  }
}
}

static void DrawClumpFinderControls(ClumpFinderWindowState& ui)
{
  ImGui::Combo("Quantity", &ui.selectedVar, kClumpFinderQuantities, kNumClumpFinderQuantities);

  ImGui::InputFloat("Density Threshold", &ui.densityThreshold, 0.0f, 1.0f);
  ImGui::InputInt("minParticles", &ui.minParticles, 10, 10);
  ImGui::InputFloat("minDepth", &ui.minDepth, 10.0f, 10.0f);

  ImGui::Checkbox("Linking Length using Hsml or cell size", &ui.useHsml);
  if (!ui.useHsml) {
    ImGui::InputFloat("Linking Length", &ui.linkingLength, 0.01f, 0.1f);
  } else {
    ImGui::InputFloat("Linking Length over cell size",
                      &ui.linkingLengthOverCellSize,
                      0.01f, 0.1f);
  }
}

static void DrawClumpFinderRunButtons(ClumpFinderWindowState& ui)
{
  if (ImGui::Button("Find Clumps")) {
    ui.requestRunFOF = true;
  }

  ImGui::SameLine();
  if (ImGui::Button("Dendrogram")) {
    ui.requestRunDendrogram = true;
  }
}

#ifdef CLUMP_DATA_READ
static void DrawClumpOutputSection(ClumpFinderWindowState& ui)
{
  ImGui::InputText("output file name",
                   ui.outputFileName,
                   IM_ARRAYSIZE(ui.outputFileName));

  if (ImGui::Button("Output clump data")) {
    ui.requestOutputHdf5 = true;
  }
}
#endif

static void DrawClumpListSection(ClumpFinderWindowState& ui)
{
  if (!ImGui::CollapsingHeader("Clump Lists")) {
    return;
  }

  ImGui::InputFloat("minimum peak density", &ui.minPeakDensity);
  ImGui::Checkbox("Leaves", &ui.showLeaves);

  ImGui::SameLine();
  enum class SortMode { Mass, Hierarchy };
  static SortMode currentSortMode = SortMode::Mass;
  if (ImGui::RadioButton("Sort by Mass", currentSortMode == SortMode::Mass)) {
    if (currentSortMode != SortMode::Mass) {
      currentSortMode = SortMode::Mass;
      ui.requestSortByMass = true;
    }
  }

  ImGui::SameLine();
  if (ImGui::RadioButton("Sort by Hierarchy", currentSortMode == SortMode::Hierarchy)) {
    if (currentSortMode != SortMode::Hierarchy) {
      currentSortMode = SortMode::Hierarchy;
      ui.requestSortByHierarchy = true;
    }
  }

  if (!ui.clumpsComputed) {
    return;
  }

  if (ImGui::BeginTable("ClumpTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Clump Info", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Hull", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableHeadersRow();

    bool hullSelectionChanged = false;
    for (size_t i = 0; i < ui.rows.size(); i++) {
      const auto& row = ui.rows[i];
      if (row.vpeak < ui.minPeakDensity) {
        continue;
      }
      if (ui.showLeaves && !row.isLeaf) {
        continue;
      }

      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      char label[256];
      std::snprintf(label, sizeof(label), "%4d   %4d    %g    position=(%.3f, %.3f, %.3f) %g",
                    row.sourceIndex, row.count, row.mass,
                    row.pos[0], row.pos[1], row.pos[2], 0.0);

      if (ImGui::Selectable(label, false)) {
        ui.requestFocusRow = true;
        ui.focusRowIndex = static_cast<int>(i);
      }

#ifdef USE_CONVEX_HULL
      ImGui::TableSetColumnIndex(1);
      char checkboxLabel[64];
      std::snprintf(checkboxLabel, sizeof(checkboxLabel), "Hull##%d", row.sourceIndex);

      bool flag = false;
      if (row.sourceIndex >= 0 &&
          row.sourceIndex < static_cast<int>(ui.showHull.size())) {
        flag = ui.showHull[static_cast<size_t>(row.sourceIndex)];
      }

      if (ImGui::Checkbox(checkboxLabel, &flag)) {
        if (row.sourceIndex >= 0 &&
            row.sourceIndex < static_cast<int>(ui.showHull.size())) {
          ui.showHull[static_cast<size_t>(row.sourceIndex)] = flag;
          hullSelectionChanged = true;
        }
      }
#endif
    }

#ifdef USE_CONVEX_HULL
    if (hullSelectionChanged) {
      ui.requestApplyHullSelection = true;
    }
#endif

    ImGui::EndTable();
  }
}

static void DrawClumpHistogramSection(ClumpFinderWindowState& ui,
                                      const PlotBatchExportViewContext& exportContext)
{
  if (!ImGui::CollapsingHeader("Clump Mass Histogram")) {
    return;
  }

  ImGui::Checkbox("Auto Range", &ui.histogramAutoRange);
  if (!ui.histogramAutoRange) {
    ImGui::InputFloat("X Axis Min", &ui.histogramRangeMin, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("X Axis Max", &ui.histogramRangeMax, 0.0f, 0.0f, "%g");
  }

  ImGui::Checkbox("Use Log scale X", &ui.histogramLogScaleX);
  ImGui::SameLine();
  ImGui::Checkbox("Use Log scale Y", &ui.histogramLogScaleY);

  ImGui::InputInt("Number of bins", &ui.histogramBins);

  ImGui::SameLine();
  ImGui::Checkbox("Use auto scale Y", &ui.histogramBinsAuto);

  if (ImGui::Button("Compute 1D Histogram")) {
    ui.requestComputeHistogram = true;
  }
  ImGui::Checkbox("Save plot image + JSON after compute", &ui.exportHistogramPackage);
  if (ui.exportFolder[0] != '\0') {
    ImGui::TextWrapped("Export folder: %s", ui.exportFolder);
  }
  if (ui.lastExportStatus[0] != '\0') {
    ImGui::TextWrapped("%s", ui.lastExportStatus);
  }

  if (!ui.histogramComputed) {
    return;
  }

  if (ImPlot::BeginPlot("Mass Histogram", ImVec2(-1, 300))) {
    if (ui.histogramLogScaleY) {
      ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
    } else {
      ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
    }

    ImPlot::SetupAxisLimits(ImAxis_X1,
                            ui.histogramRangeMin,
                            ui.histogramRangeMax,
                            ImGuiCond_Always);

    int bins_plot = ui.histogramBins;
    if (ui.histogramBinsAuto) {
      bins_plot = -1;
    }

    ImPlot::PlotHistogram("Mass",
                          ui.massHistogramValues.data(),
                          static_cast<int>(ui.massHistogramValues.size()),
                          bins_plot);
    ImPlot::EndPlot();
  }
  ExportClumpFinderHistogramIfNeeded(ui, exportContext);
}

void DrawClumpFinderUI(ClumpFinderWindowState& ui,
                       const PlotBatchExportViewContext& exportContext)
{
  if (!ui.open) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
  ImGui::Begin("clump finder", &ui.open, ImGuiWindowFlags_None);

  DrawClumpFinderControls(ui);
  DrawClumpFinderRunButtons(ui);

#ifdef CLUMP_DATA_READ
  DrawClumpOutputSection(ui);
#endif

  DrawClumpListSection(ui);
  DrawClumpHistogramSection(ui, exportContext);

  ImGui::End();
}
