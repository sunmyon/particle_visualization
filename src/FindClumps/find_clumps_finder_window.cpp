#include "FindClumps/find_clumps.h"
#include "FindClumps/clump_window_state.h"
#include "app/runtime_state.h"
#include "interaction/camera.h"

#include <imgui.h>
#include "implot.h"

namespace {
  constexpr const char* kClumpFinderQuantities[] = {
    "Density", "Temperature", "Hsml"
  };
  constexpr int kNumClumpFinderQuantities =
    sizeof(kClumpFinderQuantities) / sizeof(kClumpFinderQuantities[0]);
}

void OpenClumpFindUI(ClumpFinderWindowState& state){
  state.open = true;
}

static void DrawClumpFinderControls(ClumpFinderWindowState& ui,
                                    FindClump& cfind);

static void DrawClumpFinderRunButtons(ClumpFinderWindowState& ui,
                                      FindClump& cfind,
                                      TrackingVector<ParticleData>& originalParticles);

#ifdef CLUMP_DATA_READ
static void DrawClumpOutputSection(FindClump& cfind,
                                   TrackingVector<ParticleData>& originalParticles,
                                   const HeaderInfo& header,
                                   const SnapshotInputState& input,
                                   const SnapshotCurrentState& current);
#endif

static void DrawClumpListSection(ClumpFinderWindowState& ui,
                                 FindClump& cfind,
                                 CameraContext& cam,
                                 TrackingVector<ParticleData>& originalParticles);

static void DrawClumpHistogramSection(ClumpFinderWindowState& ui,
                                      FindClump& cfind);

void DrawClumpFinderUI(ClumpFinderWindowState& ui,
		       FindClump& cfind,
		       TrackingVector<ParticleData>& originalParticles,
		       const HeaderInfo& header,
		       const SnapshotInputState& input,
                       const SnapshotCurrentState& current,
		       CameraContext& cam)
{
  if (!ui.open) return;

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
  ImGui::Begin("clump finder", &ui.open, ImGuiWindowFlags_None);

  DrawClumpFinderControls(ui, cfind);
  DrawClumpFinderRunButtons(ui, cfind, originalParticles);

#ifdef CLUMP_DATA_READ
  DrawClumpOutputSection(cfind, originalParticles, header, input, current);
#endif

  DrawClumpListSection(ui, cfind, cam, originalParticles);
  DrawClumpHistogramSection(ui, cfind);

  ImGui::End();
}


static void DrawClumpFinderControls(ClumpFinderWindowState& ui, FindClump& cfind)
{
  ImGui::Combo("Quantity", &ui.selectedVar, kClumpFinderQuantities, kNumClumpFinderQuantities);

  auto& p = cfind.params();  
  ImGui::InputFloat("Density Threshold", &p.densityThreshold, 0.0f, 1.0f);
  ImGui::InputInt("minParticles", &p.minParticles, 10, 10);
  ImGui::InputFloat("minDepth", &p.minDepth, 10.0f, 10.0f);

  ImGui::Checkbox("Linking Length using Hsml or cell size", &p.useHsml);

  if (!p.useHsml) {
    ImGui::InputFloat("Linking Length", &p.linkingLength, 0.01f, 0.1f);
  } else {
    ImGui::InputFloat("Linking Length over cell size",
                      &p.linkingLength_over_cell_size,
                      0.01f, 0.1f);
  }
}


static void DrawClumpFinderRunButtons(ClumpFinderWindowState& ui,
                                      FindClump& cfind,
                                      TrackingVector<ParticleData>& originalParticles)
{
  std::string var = kClumpFinderQuantities[ui.selectedVar];

  if (ImGui::Button("Find Clumps")) {
    cfind.runFOF(originalParticles, var);
  }

  ImGui::SameLine();
  if (ImGui::Button("Dendrogram")) {
    cfind.runDendrogram(originalParticles, var);    
  }
}

#ifdef CLUMP_DATA_READ
static void DrawClumpOutputSection(FindClump& cfind,
                                   TrackingVector<ParticleData>& originalParticles,
                                   const HeaderInfo& header,
                                   const SnapshotInputState& input,
                                   const SnapshotCurrentState& current)
{
  static char buf[255] = "clumpList.hdf5";
  ImGui::InputText("output file name", buf, IM_ARRAYSIZE(buf));

  char temp[513];
  std::snprintf(temp, sizeof(temp), "%s/%s", input.folderPath, buf);

  if (ImGui::Button("Output clump data")) {
    std::string filename(temp);
    int snapshotIndex = current.loadedFileIndex;
    if (snapshotIndex < 0) snapshotIndex = 0;
    cfind.writeFOFtoHDF5(originalParticles, header, filename, snapshotIndex);
  }
}
#endif

static void DrawClumpListSection(ClumpFinderWindowState& ui,
                                 FindClump& cfind,
                                 CameraContext& cam,
                                 TrackingVector<ParticleData>& originalParticles)
{
  if (!ImGui::CollapsingHeader("Clump Lists"))
    return;

  ImGui::InputFloat("minimum peak density", &ui.minPeakDensity);
  ImGui::Checkbox("Leaves", &ui.showLeaves);

  ImGui::SameLine();
  
  enum class SortMode { Mass, Hierarchy };  
  static SortMode currentSortMode = SortMode::Mass;
  if (ImGui::RadioButton("Sort by Mass", currentSortMode == SortMode::Mass)) {
    if (currentSortMode != SortMode::Mass) {
      currentSortMode = SortMode::Mass;
      cfind.sortNodesByMass();
    }
  }

  ImGui::SameLine();

  if (ImGui::RadioButton("Sort by Hierarchy", currentSortMode == SortMode::Hierarchy)) {
    if (currentSortMode != SortMode::Hierarchy) {
      currentSortMode = SortMode::Hierarchy;
      cfind.sortNodesByHierarchy();
    }
  }

  if (!cfind.computed())
    return;

  if (ImGui::BeginTable("ClumpTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Clump Info", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Hull", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableHeadersRow();

#ifdef USE_CONVEX_HULL
    bool hullSelectionChanged = false;
#endif

    for (size_t i = 0; i < cfind.nodes().size(); i++) {
      const StructureNode* node = cfind.node(i);
      if (node->vpeak < ui.minPeakDensity)
        continue;

      if (ui.showLeaves && !node->isLeaf())
        continue;

      int count = node->count;
      double mass = node->totalMass;
      double pos[3] = {
        node->pos_cm[0],
        node->pos_cm[1],
        node->pos_cm[2]
      };
      double radius = 0.0;

      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      char label[256];
      std::snprintf(label, sizeof(label), "%4ld   %4d    %g    (%.3f, %.3f, %.3f) %g",
                    static_cast<long>(i), count, mass, pos[0], pos[1], pos[2], radius);

      if (ImGui::Selectable(label, false)) {
        float dist = glm::length(cam.cameraPos - cam.cameraTarget);
        glm::vec3 direction = cam.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
        cam.cameraTarget = glm::vec3(pos[0], pos[1], pos[2]);
        cam.cameraPos = cam.cameraTarget - direction * dist;
      }

#ifdef USE_CONVEX_HULL
      ImGui::TableSetColumnIndex(1);
      char checkboxLabel[64];
      std::snprintf(checkboxLabel, sizeof(checkboxLabel), "Hull##%ld", static_cast<long>(i));

      bool flag = cfind.showHull(i);
      if (ImGui::Checkbox(checkboxLabel, &flag)) {
	cfind.setShowHull(static_cast<int>(i), flag);
	hullSelectionChanged = true;
      }
#endif      
    }

#ifdef USE_CONVEX_HULL
    if (hullSelectionChanged) {
      cfind.applyHullSelectionToParticles(originalParticles);
    }
#endif

    ImGui::EndTable();
  }
}

static void DrawClumpHistogramSection(ClumpFinderWindowState& ui, FindClump& cfind)
{
  if (!ImGui::CollapsingHeader("Clump Mass Histogram"))
    return;

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
    float massMin = 0.0f;
    float massMax = 1.0f;
    
    cfind.buildMassHistogram(ui.histogramLogScaleX, massMin, massMax);

    if (ui.histogramAutoRange && cfind.histogramComputed()) {
      ui.histogramRangeMin = massMin;
      ui.histogramRangeMax = massMax;
    }
  }

  if (!cfind.histogramComputed())
    return;

  if (ImPlot::BeginPlot("Mass Histogram", ImVec2(-1, 300))) {
    if (ui.histogramLogScaleY)
      ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
    else
      ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);

    ImPlot::SetupAxisLimits(ImAxis_X1,
                            ui.histogramRangeMin,
                            ui.histogramRangeMax,
                            ImGuiCond_Always);

    int bins_plot = ui.histogramBins;
    if (ui.histogramBinsAuto)
      bins_plot = -1;

    ImPlot::PlotHistogram("Mass",
                          cfind.massHistogramValues().data(),
                          static_cast<int>(cfind.massHistogramValues().size()),
                          bins_plot);
    ImPlot::EndPlot();
  }
}
