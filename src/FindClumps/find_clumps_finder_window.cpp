#include "FindClumps/clump_window_state.h"

#include <cstdio>
#include <imgui.h>
#include "implot.h"

namespace {
constexpr const char* kClumpFinderQuantities[] = {
  "Density", "Temperature", "Hsml"
};
constexpr int kNumClumpFinderQuantities =
  static_cast<int>(sizeof(kClumpFinderQuantities) / sizeof(kClumpFinderQuantities[0]));
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
      std::snprintf(label, sizeof(label), "%4d   %4d    %g    (%.3f, %.3f, %.3f) %g",
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

static void DrawClumpHistogramSection(ClumpFinderWindowState& ui)
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
}

void DrawClumpFinderUI(ClumpFinderWindowState& ui)
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
  DrawClumpHistogramSection(ui);

  ImGui::End();
}
