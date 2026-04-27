#include <string>
#include <limits>
#include <cmath>
#include <cstring>

#include <imgui.h>
#include "implot.h"

#include "FindClumps/clump_window_state.h"
#include "FindClumps/loaded_clump_tool.h"
#ifdef CLUMP_DATA_READ
#include "FindClumps/find_clumps_IO.h"
#endif
#include "data/clump_store.h"
#include "app/state/tracking_view_state.h"
#include "interaction/camera.h"

namespace {
  const char* kEvolutionQuantities[] = {
    "Density", "Temperature", "ClumpMass", "StellarMass"
  };
  constexpr int kNumEvolutionQuantities =
    sizeof(kEvolutionQuantities) / sizeof(kEvolutionQuantities[0]);
}

void LoadedClumpTool::clearEvolutionCache() {
  evolutionCache_.clear();
  needCacheUpdate_ = false;
}

void LoadedClumpTool::rebuildEvolutionCache(const LoadedClumpWindowState& ui,
                                            const ClumpStore& clumpStore,
                                            int currentFileIndex,
                                            float& outTMin,
                                            float& outTMax,
                                            float& outValMin,
                                            float& outValMax)
{
  evolutionCache_.clear();

  outTMin   =  std::numeric_limits<float>::infinity();
  outTMax   = -std::numeric_limits<float>::infinity();
  outValMin =  std::numeric_limits<float>::infinity();
  outValMax = -std::numeric_limits<float>::infinity();

#ifndef CLUMP_DATA_READ
  (void)ui;
  (void)clumpStore;
  (void)currentFileIndex;
  needCacheUpdate_ = false;
  return;
#else
  if (!clumpStore.loaded()) {
    needCacheUpdate_ = false;
    return;
  }

  int qidx = ui.selectedEvolutionVar;
  if (qidx < 0 || qidx >= kNumEvolutionQuantities) {
    needCacheUpdate_ = false;
    return;
  }

  const std::string var = kEvolutionQuantities[qidx];

  for (size_t i = 0; i < clumpStore.size(); ++i) {
    if (i >= ui.showEvolve.size() || !ui.showEvolve[i]) {
      continue;
    }

    const auto& ch = clumpStore.clump(static_cast<int>(i));

    TrackingVector<float> times;
    TrackingVector<ClumpData> clumps;

    readClumpEvolution(clumpStore.filePath(),
                       currentFileIndex,
                       ui.finalFileIndex,
                       ui.dsnapshot,
                       ch.clumpID,
                       times,
                       clumps);

    if (times.empty() || times.size() != clumps.size()) {
      continue;
    }

    ClumpEvolutionCache cache;
    cache.index = static_cast<int>(i);
    cache.timeFloats.resize(times.size());
    cache.valueFloats.resize(times.size());

    for (size_t j = 0; j < times.size(); ++j) {
      const float t = times[j];
      const float v = clumps[j].getValue(var);

      cache.timeFloats[j] = t;
      cache.valueFloats[j] = v;

      if (t < outTMin) outTMin = t;
      if (t > outTMax) outTMax = t;

      if (ui.useLogScaleY && v <= 0.0f) {
        continue;
      }

      if (v < outValMin) outValMin = v;
      if (v > outValMax) outValMax = v;
    }

    evolutionCache_.push_back(std::move(cache));
  }

  if (evolutionCache_.empty()) {
    outTMin = 0.0f;
    outTMax = 1.0f;
    outValMin = 0.0f;
    outValMax = 1.0f;
  } else {
    if (outTMin == outTMax) outTMax = outTMin + 1.0f;
    if (outValMin == outValMax) outValMax = outValMin + 1.0f;
  }

  needCacheUpdate_ = false;
#endif
}

static void DrawClumpFileLoadSection(LoadedClumpWindowState& ui);

static void DrawLoadedClumpTable(LoadedClumpWindowState& ui);

static void DrawClumpEvolutionControls(LoadedClumpWindowState& ui);

static void DrawClumpEvolutionPlot(LoadedClumpWindowState& ui);

void DrawClumpListUI(LoadedClumpWindowState& ui)
{
  if (!ui.open) return;

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
  ImGui::Begin("Clump lists", &ui.open, ImGuiWindowFlags_None);

  DrawClumpFileLoadSection(ui);
  DrawLoadedClumpTable(ui);
  DrawClumpEvolutionControls(ui);
  DrawClumpEvolutionPlot(ui);

  ImGui::End();
}

static void DrawClumpFileLoadSection(LoadedClumpWindowState& ui)
{
  ImGui::InputText("File name of clumpList",
                   ui.clumpListPath,
                   IM_ARRAYSIZE(ui.clumpListPath));

  ImGui::SameLine();
  if (ImGui::Button("path")) {
    ui.requestUseInputPath = true;
  }

  if (ImGui::Button("read clump list")) {
    ui.requestReload = true;
  }

  if (ImGui::Button("follow clump center") && ui.selectedClumpID >= 0) {
    ui.requestFollowSelected = true;
  }
}

static void DrawLoadedClumpTable(LoadedClumpWindowState& ui)
{
  if (ui.rows.empty())
    return;

  if (ImGui::BeginTable("ClumpTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Clump Info", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Select", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Evolve", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < ui.rows.size(); i++) {
      const auto& cp = ui.rows[i];

      char label[256];
      std::snprintf(label, sizeof(label),
                    "%4zu   %4d    %g  %g  (%.3f, %.3f, %.3f)  %d  %.3g %d",
                    i, cp.count, cp.mass, cp.density,
                    cp.pos[0], cp.pos[1], cp.pos[2],
                    cp.stellarCount, cp.stellarMass, cp.stellarID);

      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      if (ImGui::Selectable(label, false)) {
        ui.requestFocusSelected = true;
        ui.focusClumpIndex = static_cast<int>(i);
      }

      ImGui::TableSetColumnIndex(1);
      if (ImGui::RadioButton(("##clumpSel" + std::to_string(i)).c_str(),
                             ui.selectedClumpID == static_cast<int>(i))) {
        ui.selectedClumpID = static_cast<int>(i);
      }
      
      ImGui::TableSetColumnIndex(2);
      std::string checkboxLabel = "##evolve" + std::to_string(i);
      bool tmp = (i < ui.showEvolve.size()) ? ui.showEvolve[i] : false;
      if (ImGui::Checkbox(checkboxLabel.c_str(), &tmp)) {
        if (i >= ui.showEvolve.size()) {
          ui.showEvolve.resize(ui.rows.size(), false);
        }
        ui.showEvolve[i] = tmp;
      }
    }

    ImGui::EndTable();
  }
}


static void DrawClumpEvolutionControls(LoadedClumpWindowState& ui)
{
  ImGui::InputInt("final snapshot index", &ui.finalFileIndex);
  ImGui::InputInt("snapshot interval", &ui.dsnapshot);

  ImGui::Checkbox("Auto Range time", &ui.autoRangeX);
  if (!ui.autoRangeX) {
    ImGui::InputFloat("time Min", &ui.tMinInput, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("time Max", &ui.tMaxInput, 0.0f, 0.0f, "%g");
  }

  ImGui::Combo("Quantity",
	       &ui.selectedEvolutionVar,
	       kEvolutionQuantities,
	       kNumEvolutionQuantities);

  ImGui::Checkbox("Use Log scale Y", &ui.useLogScaleY);

  ImGui::Checkbox("Auto Range for value (Y-axis)", &ui.autoRangeY);
  if (!ui.autoRangeY) {
    ImGui::InputFloat("val Min", &ui.valMinInput, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("val Max", &ui.valMaxInput, 0.0f, 0.0f, "%g");
  }

  if (ImGui::Button("Plot Clump Evolution")) {
    ui.requestUpdateEvolutionCache = true;
    ui.showEvolutionPlot = true;
  }
}

static void DrawClumpEvolutionPlot(LoadedClumpWindowState& ui)
{
  if (!ui.showEvolutionPlot)
    return;

  std::string var = kEvolutionQuantities[ui.selectedEvolutionVar];

  if (ImPlot::BeginPlot("Time Evolution", ImVec2(-1, 300), ImPlotFlags_None)) {
    ImPlot::SetupAxis(ImAxis_X1, "Time", ImPlotAxisFlags_None);
    ImPlot::SetupAxis(ImAxis_Y1, var.c_str(), ImPlotAxisFlags_None);

    if (ui.useLogScaleY)
      ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);

    ImPlot::SetupAxisLimits(ImAxis_X1, ui.tMinInput, ui.tMaxInput, ImGuiCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, ui.valMinInput, ui.valMaxInput, ImGuiCond_Always);

    for (const auto& cache : ui.evolutionCache) {
      std::string label = "Clump " + std::to_string(cache.clumpID);

      ImPlot::PlotLine(label.c_str(),
                       cache.timeFloats.data(),
                       cache.valueFloats.data(),
                       static_cast<int>(cache.timeFloats.size()));
    }

    ImPlot::EndPlot();
  }
}
