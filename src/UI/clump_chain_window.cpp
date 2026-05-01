#include <imgui.h>
#include "implot.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <glm/vec3.hpp>
#include "app/state/clump_window_state.h"
#include "app/state/plot_export_state.h"
#include "app/state/runtime_state.h"
#include "analysis/profile_histogram_export.h"

#include <string>

namespace {
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
}

static void DrawSelectedClumpChainNavigation(ClumpChainWindowState& ui,
                                             const SnapshotNavigationState& nav,
                                             double currentTime)
{
  ImGui::BeginDisabled(ui.selectedChainIndex == -1);

  if (ui.selectedChainIndex >= 0 &&
      ui.selectedChainIndex < static_cast<int>(ui.series.size())) {
    const auto& selected = ui.series[ui.selectedChainIndex];

    if (ImGui::Button("Load Selected Chain")) {
      ui.requestLoadSelected = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Prev")) {
      ui.requestPrev = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Next")) {
      ui.requestNext = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("from fixed viewpoint")) {
      ui.requestFixedView = true;
    }

    ImGui::Text("current snapshot index: %d (init=%d now=%d step=%d) time=%g",
                nav.initialIndex + (selected.firstSnapshot + ui.currentSnapshotIndex) * nav.skipStep,
                selected.firstSnapshot,
                selected.firstSnapshot + ui.currentSnapshotIndex,
                nav.skipStep,
                currentTime);
  }

  ImGui::EndDisabled();
}

static float GetChainValue(const ClumpChainSnapshotView& s, int selectedVar)
{
  switch (selectedVar) {
  case 0: return s.density;
  case 1: return s.temperature;
  case 2: return s.mass;
  case 3: return s.stellarMass;
  default: return s.density;
  }
}

static void DrawVerticalDashedLine(double x_value,
                                   const ImU32& col,
                                   float thickness,
                                   float dash_length,
                                   float gap_length)
{
  ImPlotRect limits = ImPlot::GetPlotLimits();
  ImVec2 p0 = ImPlot::PlotToPixels(ImVec2(x_value, limits.Y.Min));
  ImVec2 p1 = ImPlot::PlotToPixels(ImVec2(x_value, limits.Y.Max));

  float y0 = p0.y;
  float y1 = p1.y;
  if (y1 < y0) {
    float tmp = y0;
    y0 = y1;
    y1 = tmp;
  }

  ImDrawList* draw_list = ImPlot::GetPlotDrawList();
  if (!draw_list) return;

  float current_y = y0;
  while (current_y < y1) {
    float seg_end = current_y + dash_length;
    if (seg_end > y1) seg_end = y1;
    draw_list->AddLine(ImVec2(p0.x, current_y), ImVec2(p0.x, seg_end), col, thickness);
    current_y += dash_length + gap_length;
  }
}

static void ExportClumpChainEvolutionIfNeeded(ClumpChainWindowState& ui,
                                              const PlotBatchExportViewContext& exportContext)
{
  if (!ui.exportEvolutionPackage ||
      !ui.computed ||
      ui.evolutionVersion == ui.lastExportedEvolutionVersion) {
    return;
  }

  const char* quantities[] = { "Density", "Temperature", "ClumpMass", "StellarMass" };
  const int qIndex = std::clamp(ui.selectedVar, 0, static_cast<int>(IM_ARRAYSIZE(quantities)) - 1);

  std::vector<PlotLineSeries> series;
  for (size_t i = 0; i < ui.series.size(); ++i) {
    const auto& source = ui.series[i];
    if (!source.plot && static_cast<int>(i) != ui.selectedChainIndex) {
      continue;
    }
    PlotLineSeries item;
    item.label = "Chain " + std::to_string(source.globalId);
    item.x.reserve(source.snapshots.size());
    item.y.reserve(source.snapshots.size());
    for (const auto& snap : source.snapshots) {
      item.x.push_back(snap.time);
      item.y.push_back(GetChainValue(snap, ui.selectedVar));
    }
    series.push_back(std::move(item));
  }
  if (series.empty()) return;

  LineSeriesPlotExportParams params;
  params.kind = "clump_chain_evolution";
  params.title = "Clump Chain Evolution";
  params.xLabel = "Time";
  params.yLabel = quantities[qIndex];
  params.logY = ui.useLogScaleY;
  params.xMin = ui.xmin;
  params.xMax = ui.xmax;
  params.yMin = ui.ymin;
  params.yMax = ui.ymax;

  const std::filesystem::path dir =
    EnsureExportFolder(ui.exportFolder, sizeof(ui.exportFolder));
  const std::string stem =
    "clump_chain_evolution_" +
    std::to_string(static_cast<unsigned long long>(ui.evolutionVersion));
  AnalysisPlotExportSpec spec = MakeSpec(exportContext, dir, stem);
  AnalysisPlotExportResult result =
    ExportLineSeriesPlotPackage(spec, params, series);
  StoreStatus(ui.lastExportStatus, sizeof(ui.lastExportStatus), result);
  if (result.ok) {
    ui.lastExportedEvolutionVersion = ui.evolutionVersion;
  }
}

static void DrawSelectedClumpChainPlot(ClumpChainWindowState& ui,
                                       double time,
                                       const PlotBatchExportViewContext& exportContext)
{
  if (ui.selectedChainIndex < 0 ||
      ui.selectedChainIndex >= static_cast<int>(ui.series.size())) {
    return;
  }

  const char* quantities[] = { "Density", "Temperature", "ClumpMass", "StellarMass" };
  ImGui::Combo("Quantity", &ui.selectedVar, quantities, IM_ARRAYSIZE(quantities));
  std::string var = quantities[ui.selectedVar];

  ImGui::Checkbox("Use Log scale Y", &ui.useLogScaleY);
  ImGui::Checkbox("Use autoscale", &ui.autoScale);
  ImGui::Checkbox("Save plot image + JSON after draw", &ui.exportEvolutionPackage);
  if (ui.exportFolder[0] != '\0') {
    ImGui::TextWrapped("Export folder: %s", ui.exportFolder);
  }
  if (ui.lastExportStatus[0] != '\0') {
    ImGui::TextWrapped("%s", ui.lastExportStatus);
  }

  if (!ui.autoScale) {
    ImGui::InputFloat("X Axis Min", &ui.xmin, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("X Axis Max", &ui.xmax, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Min", &ui.ymin, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Max", &ui.ymax, 0.0f, 0.0f, "%g");
  }

  if (ImPlot::BeginPlot("Time Evolution", ImVec2(-1, 300), ImPlotFlags_None)) {
    ImPlot::SetupAxis(ImAxis_X1, "Time", ImPlotAxisFlags_None);
    ImPlot::SetupAxis(ImAxis_Y1, var.c_str(), ImPlotAxisFlags_None);

    if (ui.useLogScaleY) {
      ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
    } else {
      ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
    }

    if (ui.autoScale) {
      float time_max = -1.e20f, value_max = -1.e20f;
      float time_min =  1.e20f, value_min =  1.e20f;

      for (size_t i = 0; i < ui.series.size(); i++) {
        const auto& series = ui.series[i];
        if (!series.plot && static_cast<int>(i) != ui.selectedChainIndex) {
          continue;
        }
        for (const auto& snap : series.snapshots) {
          if (time_max < snap.time) time_max = snap.time;
          if (time_min > snap.time) time_min = snap.time;

          const float value = GetChainValue(snap, ui.selectedVar);
          if (ui.useLogScaleY && value <= 0.0f) {
            continue;
          }
          if (value_max < value) value_max = value;
          if (value_min > value) value_min = value;
        }
      }

      ui.xmax = time_max;
      ui.xmin = time_min;
      ui.ymax = value_max;
      ui.ymin = value_min;
    }

    ImPlot::SetupAxisLimits(ImAxis_X1, ui.xmin, ui.xmax, ImGuiCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, ui.ymin, ui.ymax, ImGuiCond_Always);

    for (size_t i = 0; i < ui.series.size(); i++) {
      const auto& series = ui.series[i];
      if (!series.plot && static_cast<int>(i) != ui.selectedChainIndex) {
        continue;
      }

      std::vector<float> times;
      std::vector<float> values;
      times.reserve(series.snapshots.size());
      values.reserve(series.snapshots.size());
      for (const auto& snap : series.snapshots) {
        times.push_back(snap.time);
        values.push_back(GetChainValue(snap, ui.selectedVar));
      }

      std::string label = "Chain " + std::to_string(series.globalId);
      ImPlot::PlotLine(label.c_str(), times.data(), values.data(), static_cast<int>(times.size()));
    }

    ImU32 red = ImGui::GetColorU32(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    DrawVerticalDashedLine(time, red, 1.0f, 5.0f, 3.0f);
    ImPlot::EndPlot();
  }
  ExportClumpChainEvolutionIfNeeded(ui, exportContext);
}

static void DrawSelectedClumpProjectionSection(ClumpChainWindowState& ui)
{
  ImGui::PushItemWidth(100);
  ImGui::InputFloat("len (original)", &ui.mapLen, 0.0f, 0.0f, "%g");
  ImGui::InputFloat("val_min", &ui.mapValMin, 0.0f, 0.0f, "%g");
  ImGui::SameLine();
  ImGui::InputFloat("val_max", &ui.mapValMax, 0.0f, 0.0f, "%g");
  ImGui::InputInt("npixel", &ui.mapNpixel, 10, 1000);
  ImGui::SameLine();
  ImGui::InputInt("nslices", &ui.mapNslices, 10, 1000);
  ImGui::InputText("output directory##evolution_map",
                   ui.mapOutputDir,
                   IM_ARRAYSIZE(ui.mapOutputDir));
  ImGui::PopItemWidth();

  const char* quantities2[] = {"Density", "Temperature", "val", "val2", "Hsml", "Mass"};
  ImGui::Combo("Quantity##evo",
               &ui.selectedProjectionVar,
               quantities2,
               IM_ARRAYSIZE(quantities2));

  if (ImGui::Button("make projection maps")) {
    ui.requestMakeProjectionMaps = true;
  }

  if (ui.projectionBatchRunning) {
    ImGui::SameLine();
    ImGui::Text("running: %d", ui.projectionBatchCursor);
  }
}

void DrawClumpChainListUI(ClumpChainWindowState& ui,
                          const SnapshotNavigationState& nav,
                          const SnapshotCurrentState& current,
                          const PlotBatchExportViewContext& exportContext)
{
  if (!ui.open) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
  ImGui::Begin("Clump chain lists", &ui.open, ImGuiWindowFlags_None);

  if (ImGui::Button("extract clump evolution chain")) {
    ui.requestBuild = true;
  }

  if (ui.computed) {
    if (ImGui::BeginTable("ClumpChainTable", 10, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Chain ID", ImGuiTableColumnFlags_WidthFixed, 60);
      ImGui::TableSetupColumn("Start Snap", ImGuiTableColumnFlags_WidthFixed, 60);
      ImGui::TableSetupColumn("End Snap", ImGuiTableColumnFlags_WidthFixed, 60);
      ImGui::TableSetupColumn("Stellar ID", ImGuiTableColumnFlags_WidthFixed, 80);
      ImGui::TableSetupColumn("Stellar Mass", ImGuiTableColumnFlags_WidthFixed, 80);
      ImGui::TableSetupColumn("Stellar Count", ImGuiTableColumnFlags_WidthFixed, 80);
      ImGui::TableSetupColumn("Maximum stellar Mass", ImGuiTableColumnFlags_WidthFixed, 80);
      ImGui::TableSetupColumn("Maximum Clump Mass", ImGuiTableColumnFlags_WidthFixed, 80);
      ImGui::TableSetupColumn("Temperature", ImGuiTableColumnFlags_WidthStretch, 80);
      ImGui::TableSetupColumn("Plot", ImGuiTableColumnFlags_WidthStretch, 50);
      ImGui::TableHeadersRow();

      for (size_t idx = 0; idx < ui.series.size(); idx++) {
        auto& s = ui.series[idx];
        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(idx));

        bool isSelected = (static_cast<int>(idx) == ui.selectedChainIndex);
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Selectable(("Chain " + std::to_string(s.globalId)).c_str(),
                              isSelected,
                              ImGuiSelectableFlags_SpanAllColumns)) {
          ui.selectedChainIndex = static_cast<int>(idx);
        }
        if (isSelected) {
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                 ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.8f, 0.5f)));
        }

        ImGui::TableSetColumnIndex(1); ImGui::Text("%d", s.firstSnapshot);
        ImGui::TableSetColumnIndex(2); ImGui::Text("%d", s.lastSnapshot);
        ImGui::TableSetColumnIndex(3); ImGui::Text("%d", s.stellarId);
        ImGui::TableSetColumnIndex(4); ImGui::Text("%g", s.mstar);
        ImGui::TableSetColumnIndex(5); ImGui::Text("%d", s.nstar);
        ImGui::TableSetColumnIndex(6); ImGui::Text("%g", s.mstarMaximum);
        ImGui::TableSetColumnIndex(7); ImGui::Text("%g", s.massMaximum);
        ImGui::TableSetColumnIndex(8); ImGui::Text("%g", s.temperature_d);
        ImGui::TableSetColumnIndex(9);
        ImGui::Checkbox(("##plot" + std::to_string(idx)).c_str(), &s.plot);

        ImGui::PopID();
      }

      ImGui::EndTable();
    }

    DrawSelectedClumpChainNavigation(ui, nav, current.loadedTime);
    DrawSelectedClumpChainPlot(ui, current.loadedTime, exportContext);
    DrawSelectedClumpProjectionSection(ui);
  }

  ImGui::End();
}
