#include "app/state/clump_window_state.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
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

constexpr const char* kClumpFinderPlotQuantityLabels[] = {
  "Mass [Msun]",
  "Density",
  "Temperature",
  "Peak value",
  "Count",
  "Stellar mass [Msun]",
  "Stellar count",
  "Depth"
};

constexpr const char* kClumpFinderPlotYLabels[] = {
  "Histogram",
  "Mass [Msun]",
  "Density",
  "Temperature",
  "Peak value",
  "Count",
  "Stellar mass [Msun]",
  "Stellar count",
  "Depth"
};

const char* ClumpPlotQuantityLabel(int quantity)
{
  if (quantity < 0 || quantity >= kNumClumpFinderPlotQuantities) {
    quantity = 0;
  }
  return kClumpFinderPlotQuantityLabels[quantity];
}

std::string ClumpPlotAxisLabel(int quantity, bool logScale)
{
  std::string label = ClumpPlotQuantityLabel(quantity);
  if (logScale) {
    return "log10(" + label + ")";
  }
  return label;
}

int QuantityFromYSelection(int selection)
{
  return std::clamp(selection - 1, 0, kNumClumpFinderPlotQuantities - 1);
}

float TransformClumpAxisLimit(float value, bool logScale)
{
  if (!logScale) {
    return value;
  }
  if (!std::isfinite(value) || value <= 0.0f) {
    return 0.0f;
  }
  return std::log10(value);
}

void TransformClumpAxisRange(float minValue,
                             float maxValue,
                             bool logScale,
                             float& outMin,
                             float& outMax)
{
  outMin = TransformClumpAxisLimit(minValue, logScale);
  outMax = TransformClumpAxisLimit(maxValue, logScale);
  if (!(outMax > outMin) ||
      !std::isfinite(outMin) ||
      !std::isfinite(outMax)) {
    outMin = logScale ? -1.0f : 0.0f;
    outMax = logScale ? 1.0f : 1.0f;
  }
}

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
    if (!std::isfinite(v) || v < xmin || v > xmax) {
      continue;
    }
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
  float xMin = 0.0f;
  float xMax = 1.0f;
  TransformClumpAxisRange(ui.histogramRangeMin,
                          ui.histogramRangeMax,
                          ui.histogramLogScaleX,
                          xMin,
                          xMax);

  BuildHistogramBins(ui.histogramValues,
                     ui.histogramBins,
                     xMin,
                     xMax,
                     centers,
                     counts,
                     binSize);

  BarHistogramPlotExportParams params;
  float yMin = ui.histogramCountMin;
  float yMax = ui.histogramCountMax;
  if (!(yMax > yMin) || !std::isfinite(yMin) || !std::isfinite(yMax)) {
    yMin = ui.histogramLogScaleY ? 0.8f : 0.0f;
    yMax = 100.0f;
  }
  if (ui.histogramLogScaleY && yMin <= 0.0f) {
    yMin = 0.8f;
  }

  params.kind = "clump_finder_histogram";
  params.title = "Clump Histogram";
  params.xLabel = ClumpPlotAxisLabel(ui.histogramSelectedVar,
                                     ui.histogramLogScaleX);
  params.yLabel = "Count";
  params.logX = false;
  params.logY = ui.histogramLogScaleY;
  params.xMin = xMin;
  params.xMax = xMax;
  params.yMin = yMin;
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
  ImGui::InputFloat("Density contrast ratio",
                    &ui.minDensityContrastRatio,
                    0.1f,
                    1.0f,
                    "%.3g");

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

  auto rowPassesFilter = [&](const ClumpFinderRowView& row) {
    if (row.vpeak < ui.minPeakDensity) {
      return false;
    }
    if (ui.showLeaves && !row.isLeaf) {
      return false;
    }
    return true;
  };

  auto makeRowLabel = [](const ClumpFinderRowView& row,
                         char* label,
                         size_t labelSize) {
    const char* kind = row.isLeaf ? "leaf" : (row.isTrunk ? "trunk" : "branch");
    std::snprintf(label,
                  labelSize,
                  "#%d %s depth=%d parent=%d children=%d count=%d "
                  "mass=%.3g Msun mstar=%.3g Msun pos=(%.3g, %.3g, %.3g)",
                  row.sourceIndex,
                  kind,
                  row.depth,
                  row.parentSourceIndex,
                  row.childCount,
                  row.count,
                  row.mass,
                  row.stellarMass,
                  row.pos[0],
                  row.pos[1],
                  row.pos[2]);
  };

  std::unordered_map<int, size_t> rowBySourceIndex;
  rowBySourceIndex.reserve(ui.rows.size());
  std::unordered_map<int, std::vector<size_t>> childrenByParent;
  childrenByParent.reserve(ui.rows.size());
  std::vector<size_t> roots;
  roots.reserve(ui.rows.size());

  for (size_t i = 0; i < ui.rows.size(); ++i) {
    rowBySourceIndex[ui.rows[i].sourceIndex] = i;
  }
  for (size_t i = 0; i < ui.rows.size(); ++i) {
    const int parent = ui.rows[i].parentSourceIndex;
    if (parent >= 0 && rowBySourceIndex.find(parent) != rowBySourceIndex.end()) {
      childrenByParent[parent].push_back(i);
    } else {
      roots.push_back(i);
    }
  }

  std::function<bool(size_t)> rowOrDescendantPassesFilter = [&](size_t rowIndex) {
    if (rowPassesFilter(ui.rows[rowIndex])) {
      return true;
    }
    auto it = childrenByParent.find(ui.rows[rowIndex].sourceIndex);
    if (it == childrenByParent.end()) {
      return false;
    }
    for (size_t childIndex : it->second) {
      if (rowOrDescendantPassesFilter(childIndex)) {
        return true;
      }
    }
    return false;
  };

  if (ImGui::BeginTable("ClumpTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Clump Info", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Hull", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableHeadersRow();

    bool hullSelectionChanged = false;

    if (ui.showLeaves) {
      for (size_t i = 0; i < ui.rows.size(); ++i) {
        const auto& row = ui.rows[i];
        if (!rowPassesFilter(row)) {
          continue;
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        char label[256];
        makeRowLabel(row, label, sizeof(label));
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
      return;
    }

    std::function<void(size_t)> drawRow = [&](size_t i) {
      if (!rowOrDescendantPassesFilter(i)) {
        return;
      }
      const auto& row = ui.rows[i];
      const auto childIt = childrenByParent.find(row.sourceIndex);
      const bool hasChildren =
        childIt != childrenByParent.end() && !childIt->second.empty();
      const bool visibleRow = rowPassesFilter(row);

      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      char label[256];
      makeRowLabel(row, label, sizeof(label));

      ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth;
      if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
      }
      if (!visibleRow) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      }
      const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(
                                           static_cast<intptr_t>(row.sourceIndex)),
                                         flags,
                                         "%s",
                                         label);
      if (!visibleRow) {
        ImGui::PopStyleColor();
      }
      if (ImGui::IsItemClicked()) {
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

      if (open && hasChildren) {
        for (size_t childIndex : childIt->second) {
          drawRow(childIndex);
        }
        ImGui::TreePop();
      }
    };

    for (size_t rootIndex : roots) {
      drawRow(rootIndex);
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
  if (!ImGui::CollapsingHeader("Clump Plots")) {
    return;
  }

  ui.plotYSelection =
    std::clamp(ui.plotYSelection, 0, kNumClumpFinderPlotQuantities);
  const bool yIsHistogram = ui.plotYSelection == 0;

  ImGui::TextUnformatted("X/Y plot");
  ImGui::Combo("X##clump_plot_x",
               &ui.scatterXVar,
               kClumpFinderPlotQuantityLabels,
               kNumClumpFinderPlotQuantities);
  ImGui::SameLine();
  ImGui::Combo("Y##clump_plot_y",
               &ui.plotYSelection,
               kClumpFinderPlotYLabels,
               kNumClumpFinderPlotQuantities + 1);

  ImGui::InputFloat("X Min##clump_plot_xmin", &ui.scatterRangeXMin, 0.0f, 0.0f, "%g");
  ImGui::SameLine();
  ImGui::InputFloat("X Max##clump_plot_xmax", &ui.scatterRangeXMax, 0.0f, 0.0f, "%g");

  if (yIsHistogram) {
    ImGui::InputFloat("Count Min##clump_plot_count_min",
                      &ui.histogramCountMin,
                      0.0f,
                      0.0f,
                      "%g");
    ImGui::SameLine();
    ImGui::InputFloat("Count Max##clump_plot_count_max",
                      &ui.histogramCountMax,
                      0.0f,
                      0.0f,
                      "%g");
  } else {
    ImGui::InputFloat("Y Min##clump_plot_ymin", &ui.scatterRangeYMin, 0.0f, 0.0f, "%g");
    ImGui::SameLine();
    ImGui::InputFloat("Y Max##clump_plot_ymax", &ui.scatterRangeYMax, 0.0f, 0.0f, "%g");
  }

  ImGui::Checkbox("Log X##clump_plot_logx", &ui.scatterLogScaleX);
  ImGui::SameLine();
  if (yIsHistogram) {
    ImGui::Checkbox("Log Count##clump_plot_log_count", &ui.histogramLogScaleY);
  } else {
    ImGui::Checkbox("Log Y##clump_plot_logy", &ui.scatterLogScaleY);
  }

  if (yIsHistogram) {
    ImGui::InputInt("Number of bins##clump_hist_bins", &ui.histogramBins);
    ui.histogramBins = std::max(1, ui.histogramBins);
  }

  if (ImGui::Button("Compute Plot")) {
    if (yIsHistogram) {
      ui.histogramSelectedVar = ui.scatterXVar;
      ui.histogramRangeMin = ui.scatterRangeXMin;
      ui.histogramRangeMax = ui.scatterRangeXMax;
      ui.histogramLogScaleX = ui.scatterLogScaleX;
      ui.requestComputeHistogram = true;
    } else {
      ui.scatterYVar = QuantityFromYSelection(ui.plotYSelection);
      ui.requestComputeScatter = true;
    }
  }

  if (yIsHistogram) {
    ImGui::SameLine();
    ImGui::Checkbox("Save plot image + JSON after compute", &ui.exportHistogramPackage);
    if (ui.exportFolder[0] != '\0') {
      ImGui::TextWrapped("Export folder: %s", ui.exportFolder);
    }
    if (ui.lastExportStatus[0] != '\0') {
      ImGui::TextWrapped("%s", ui.lastExportStatus);
    }
  }

  if (yIsHistogram && ui.histogramComputed) {
    ui.histogramSelectedVar = ui.scatterXVar;
    ui.histogramRangeMin = ui.scatterRangeXMin;
    ui.histogramRangeMax = ui.scatterRangeXMax;
    ui.histogramLogScaleX = ui.scatterLogScaleX;

    float xMin = 0.0f;
    float xMax = 1.0f;
    TransformClumpAxisRange(ui.histogramRangeMin,
                            ui.histogramRangeMax,
                            ui.histogramLogScaleX,
                            xMin,
                            xMax);

    float yMin = ui.histogramCountMin;
    float yMax = ui.histogramCountMax;
    if (!(yMax > yMin) || !std::isfinite(yMin) || !std::isfinite(yMax)) {
      yMin = ui.histogramLogScaleY ? 0.8f : 0.0f;
      yMax = 100.0f;
    }
    if (ui.histogramLogScaleY && yMin <= 0.0f) {
      yMin = 0.8f;
    }

    std::vector<float> centers;
    std::vector<float> counts;
    float binSize = 1.0f;
    BuildHistogramBins(ui.histogramValues,
                       ui.histogramBins,
                       xMin,
                       xMax,
                       centers,
                       counts,
                       binSize);

    const std::string xLabel =
      ClumpPlotAxisLabel(ui.histogramSelectedVar, ui.histogramLogScaleX);
    if (ImPlot::BeginPlot("Clump Histogram", ImVec2(-1, 300))) {
      ImPlot::SetupAxis(ImAxis_X1, xLabel.c_str());
      ImPlot::SetupAxis(ImAxis_Y1, "Count");
      if (ui.histogramLogScaleY) {
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
      } else {
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
      }
      ImPlot::SetupAxisLimits(ImAxis_X1, xMin, xMax, ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, yMin, yMax, ImGuiCond_Always);

      ImPlot::PlotBars(ClumpPlotQuantityLabel(ui.histogramSelectedVar),
                       centers.data(),
                       counts.data(),
                       static_cast<int>(counts.size()),
                       binSize);
      ImPlot::EndPlot();
    }
    ExportClumpFinderHistogramIfNeeded(ui, exportContext);
  }

  if (!yIsHistogram && ui.scatterComputed) {
    float xMin = 0.0f;
    float xMax = 1.0f;
    float yMin = 0.0f;
    float yMax = 1.0f;
    TransformClumpAxisRange(ui.scatterRangeXMin,
                            ui.scatterRangeXMax,
                            ui.scatterLogScaleX,
                            xMin,
                            xMax);
    TransformClumpAxisRange(ui.scatterRangeYMin,
                            ui.scatterRangeYMax,
                            ui.scatterLogScaleY,
                            yMin,
                            yMax);
    const std::string xLabel =
      ClumpPlotAxisLabel(ui.scatterXVar, ui.scatterLogScaleX);
    const std::string yLabel =
      ClumpPlotAxisLabel(ui.scatterYVar, ui.scatterLogScaleY);
    if (ImPlot::BeginPlot("Clump Scatter", ImVec2(-1, 300))) {
      ImPlot::SetupAxis(ImAxis_X1, xLabel.c_str());
      ImPlot::SetupAxis(ImAxis_Y1, yLabel.c_str());
      ImPlot::SetupAxisLimits(ImAxis_X1, xMin, xMax, ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, yMin, yMax, ImGuiCond_Always);
      ImPlot::PlotScatter("Clumps",
                          ui.scatterXValues.data(),
                          ui.scatterYValues.data(),
                          static_cast<int>(ui.scatterXValues.size()));
      ImPlot::EndPlot();
    }
  }
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
