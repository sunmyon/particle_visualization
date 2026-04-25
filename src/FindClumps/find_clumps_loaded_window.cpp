#include <string>
#include <limits>
#include <cmath>
#include <cstring>

#include <imgui.h>
#include "implot.h"

#include "FindClumps/clump_window_state.h"
#include "FindClumps/loaded_clump_tool.h"
#include "FindClumps/find_clumps_IO.h"
#include "app/runtime_state.h"
#include "data/clump_store.h"
#include "data/clump_loader.h"
#include "app/tracking_view_state.h"
#include "app/normalization_config.h"
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
}

void OpenClumpListUI(LoadedClumpWindowState& state){
  state.open = true;
}

static void DrawClumpFileLoadSection(LoadedClumpWindowState& ui,
				     ClumpStore& clumpStore,
				     TrackingTargetState& state,
                                     int currentFileIndex,
                                     const SnapshotInputState& input,
                                     const NormalizationContext& normalization);

static void DrawLoadedClumpTable(LoadedClumpWindowState& ui,
				 ClumpStore& clumpStore,
                                 CameraContext& cam);

static void DrawClumpEvolutionControls(LoadedClumpWindowState& ui,
				       LoadedClumpTool& ctool);

static void DrawClumpEvolutionPlot(LoadedClumpWindowState& ui,
				   LoadedClumpTool& ctool,
				   ClumpStore& clumpStore,
                                   int currentFileIndex);

void DrawClumpListUI(LoadedClumpWindowState& ui,
		     LoadedClumpTool& ctool,
		     ClumpStore& clump,
		     TrackingTargetState& view,
		     int currentFileIndex,
		     const SnapshotInputState& input,
		     CameraContext& cam,
		     const NormalizationContext& normalization)
{
  if (!ui.open) return;

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
  ImGui::Begin("Clump lists", &ui.open, ImGuiWindowFlags_None);

  DrawClumpFileLoadSection(ui, clump, view, currentFileIndex, input, normalization);
  DrawLoadedClumpTable(ui, clump, cam);
  DrawClumpEvolutionControls(ui, ctool);
  DrawClumpEvolutionPlot(ui, ctool, clump, currentFileIndex);

  ImGui::End();
}

static void DrawClumpFileLoadSection(LoadedClumpWindowState& ui,
				     ClumpStore& clumpStore,
				     TrackingTargetState& view,
                                     int currentFileIndex,
                                     const SnapshotInputState& input,
                                     const NormalizationContext& normalization)
{
  static char buf[255];
  ImGui::InputText("File name of clumpList", buf, IM_ARRAYSIZE(buf));

  ImGui::SameLine();
  if (ImGui::Button("path")) {
    std::strcpy(buf, input.folderPath);
  }

  if (view.renewAfterSnapshot) {
    if (view.followClump) 
      ui.selectedClumpID = clumpStore.findIndexByClumpID(view.targetClumpID);
    
    view.renewAfterSnapshot = false;
  }

  if (ImGui::Button("read clump list")) {
    clumpStore.setFilePath(buf);
    auto clumps = loadClumpData(clumpStore.filePath().c_str(),
                                currentFileIndex,
                                normalization.toNormalizedScale());
    
    if (!clumps.empty()) {
      clumpStore.setClumps(std::move(clumps));
      ui.showEvolve.resize(clumpStore.size(), false);
    }
  }

  if (ImGui::Button("follow clump center") && ui.selectedClumpID >= 0) {
    view.targetClumpID = clumpStore.clump(ui.selectedClumpID).clumpID;
    view.followClump = true;
    view.followParticle = false;
  }
}

static void DrawLoadedClumpTable(LoadedClumpWindowState& ui,
				 ClumpStore& clumpStore,
                                 CameraContext& cam)
{
  if (!clumpStore.loaded())
    return;

  if (ImGui::BeginTable("ClumpTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Clump Info", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Select", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Evolve", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < clumpStore.size(); i++) {
      const auto& cp = clumpStore.clump(i);

      char label[256];
      std::snprintf(label, sizeof(label),
                    "%4zu   %4d    %g  %g  (%.3f, %.3f, %.3f)  %d  %.3g %d",
                    i, cp.count, cp.mass, cp.density,
                    cp.Pos[0], cp.Pos[1], cp.Pos[2],
                    cp.stellar_count, cp.stellar_mass, cp.stellar_id);

      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      if (ImGui::Selectable(label, false)) {
        float dist = glm::length(cam.cameraPos - cam.cameraTarget);
        glm::vec3 direction = cam.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
        cam.cameraTarget = glm::vec3(cp.Pos[0], cp.Pos[1], cp.Pos[2]);
        cam.cameraPos = cam.cameraTarget - direction * dist;
      }

      ImGui::TableSetColumnIndex(1);
      if (ImGui::RadioButton(("##clumpSel" + std::to_string(i)).c_str(),
                             ui.selectedClumpID == static_cast<int>(i))) {
        ui.selectedClumpID = static_cast<int>(i);
      }
      
      ImGui::TableSetColumnIndex(2);
      std::string checkboxLabel = "##evolve" + std::to_string(i);
      bool tmp = ui.showEvolve[i];
      if (ImGui::Checkbox(checkboxLabel.c_str(), &tmp)) {
        ui.showEvolve[i] = tmp;
      }
    }

    ImGui::EndTable();
  }
}


static void DrawClumpEvolutionControls(LoadedClumpWindowState& ui, LoadedClumpTool& ctool)
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
    ctool.requestEvolutionPlotUpdate();
  }
}

static void DrawClumpEvolutionPlot(LoadedClumpWindowState& ui,
                                   LoadedClumpTool& ctool,
                                   ClumpStore& clumpStore,
                                   int currentFileIndex)
{
  float tMin = 0.0f, tMax = 1.0f;
  float valMin = 0.0f, valMax = 1.0f;

  if (ctool.needCacheUpdate()) {
    ctool.rebuildEvolutionCache(ui, clumpStore, currentFileIndex,
                                tMin, tMax, valMin, valMax);

    if (ui.autoRangeX) {
      ui.tMinInput = tMin;
      ui.tMaxInput = tMax;
    }
    if (ui.autoRangeY) {
      ui.valMinInput = valMin;
      ui.valMaxInput = valMax;
    }
  }

  if (!ctool.showEvolution())
    return;

  std::string var = kEvolutionQuantities[ui.selectedEvolutionVar];

  if (ImPlot::BeginPlot("Time Evolution", ImVec2(-1, 300), ImPlotFlags_None)) {
    ImPlot::SetupAxis(ImAxis_X1, "Time", ImPlotAxisFlags_None);
    ImPlot::SetupAxis(ImAxis_Y1, var.c_str(), ImPlotAxisFlags_None);

    if (ui.useLogScaleY)
      ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);

    ImPlot::SetupAxisLimits(ImAxis_X1, ui.tMinInput, ui.tMaxInput, ImGuiCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, ui.valMinInput, ui.valMaxInput, ImGuiCond_Always);

    for (const auto& cache : ctool.evolutionCache()) {
      int index = cache.index;
      const auto& ch = clumpStore.clump(index);
      std::string label = "Clump " + std::to_string(ch.clumpID);

      ImPlot::PlotLine(label.c_str(),
                       cache.timeFloats.data(),
                       cache.valueFloats.data(),
                       static_cast<int>(cache.timeFloats.size()));
    }

    ImPlot::EndPlot();
  }
}
