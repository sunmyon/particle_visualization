#include <unordered_set>

#include "UI.h"
#include <imgui.h>
#include "implot.h"

#include "core/tracking_vector.h"
#include "interaction/camera.h"
#include "object.h"
#include "FileIO/file_io.h"
#include "render/colormap_defs.h"
#include "render/gizmo_renderer.h"
#include "data/particle_array.h"

extern void UpdateCuboidTransformArcball(CuboidObject& cuboid,
                                         float oldX, float oldY,
                                         float newX, float newY,
					 float screenWidth, float screenHeight,
                                         const glm::mat4& view,
                                         const glm::vec3& pivot);

namespace {
  RadialProfileUIState gRadialProfileUIState;
  Histogram2DUIState   gHistogram2DUIState;
  ProjectionMapUIState gProjectionMapUIState;
  TopParticlesUIState gTopParticlesUIState;
  HaloesUIState gHaloesUIState;
  MaskUIState gMaskUIState;
}

void OpenRadialProfileUI() {
  gRadialProfileUIState.open = true;
}


void DrawRadialProfileUI(RadialProfileComputer& computer,
                         const ParticleBlock& partblock,
                         double unitmass_in_g,
                         double unitlength_in_cm,
                         double unittime_in_s)
{
  auto& state = gRadialProfileUIState;
  if (!state.open) return;

  computer.setUnits(unitmass_in_g, unitlength_in_cm, unittime_in_s);

  ImGui::Begin("Radial Profile", &state.open);

  const char* xaxes[] = { "r", "x", "y", "z", "M(<r)" };
  ImGui::Combo("X Axis", &state.selectedXAxis, xaxes, IM_ARRAYSIZE(xaxes));
  state.params.xmode = (XAxisMode)state.selectedXAxis;

  const int baseCount = partblock.nUIQ;
  const bool allowMDot = (state.params.xmode == XAxisMode::Radius ||
                          state.params.xmode == XAxisMode::EnclosedMass);
  const int totalCount = baseCount + (allowMDot ? 1 : 0);

  if (state.selectedVarIdx < 0 || state.selectedVarIdx >= totalCount)
    state.selectedVarIdx = 0;

  auto labelAt = [&](int idx)->const char* {
    if (idx < baseCount) return QuantityLabel(partblock.uiQ[idx]);
    return "mdot";
  };

  if (ImGui::BeginCombo("Quantity", labelAt(state.selectedVarIdx))) {
    for (int i = 0; i < baseCount; ++i) {
      bool sel = (state.selectedVarIdx == i);
      if (ImGui::Selectable(QuantityLabel(partblock.uiQ[i]), sel))
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
    state.params.var1 = partblock.uiQ[state.selectedVarIdx];
    state.params.isMDot = false;
  } else {
    state.params.isMDot = true;
  }

  ImGui::InputInt("Number of Bins", &state.params.bins);
  ImGui::Checkbox("Use Original Coordinates", &state.params.useOriginal);
  ImGui::Checkbox("Log X Axis", &state.params.plotXAxisLog);
  ImGui::Checkbox("Log Y Axis", &state.params.plotYAxisLog);
  ImGui::Checkbox("Auto Range", &state.params.autorange);
  ImGui::Checkbox("Take absolute value", &state.params.flagAbsolute);

  if (!state.params.autorange) {
    ImGui::InputFloat("X Axis Min", &state.params.xmin, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("X Axis Max", &state.params.xmax, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Min", &state.params.ymin, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Max", &state.params.ymax, 0.0f, 0.0f, "%g");
  }

  ImGui::InputFloat("Maximum Radius (cut)", &state.params.rmax, 0.0f, 0.0f, "%g");

  if (ImGui::Button("Compute profile")) {
    state.result = computer.compute(partblock, state.params);

    if (state.params.autorange) {
      state.params.xmin = state.result.xmin;
      state.params.xmax = state.result.xmax;
      state.params.ymin = state.result.ymin;
      state.params.ymax = state.result.ymax;
    }

    state.computed = true;
  }

  if (state.computed && state.result.valid) {
    if (ImPlot::BeginPlot("Profile", ImVec2(-1, 300))) {
      const char* ylabel = state.params.isMDot ? "mdot" : QuantityLabel(state.params.var1);
      ImPlot::SetupAxes(XAxisLabel(state.params.xmode), ylabel);

      if (state.params.plotXAxisLog)
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
      if (state.params.plotYAxisLog)
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);

      ImPlot::SetupAxisLimits(ImAxis_X1, state.params.xmin, state.params.xmax, ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, state.params.ymin, state.params.ymax, ImGuiCond_Always);

      ImPlot::PlotLine("Profile",
                       state.result.x.data(),
                       state.result.y.data(),
                       (int)state.result.y.size());
      ImPlot::EndPlot();
    }
  }

  ImGui::End();
}

void OpenHistogram2DUI() {
  gHistogram2DUIState.open = true;
}

void DrawHistogram2DUI(Histogram2DComputer& computer,
                       ParticleBlock& partblock,
		       const Histogram2DContext& ctx)
{
  auto& state = gHistogram2DUIState;
  if (!state.open) return;

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
  ImGui::Begin("histogram 2D", &state.open, ImGuiWindowFlags_None);

  if (ImGui::BeginCombo("X Axis Quantity", QuantityLabel(state.params.var1))) {
    for (int q = 0; q < partblock.nAllQ; ++q) {
      QuantityId cand = partblock.allQ[q];
      bool is_selected = (cand == state.params.var1);
      if (ImGui::Selectable(QuantityLabel(cand), is_selected))
        state.params.var1 = cand;
      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  if (ImGui::BeginCombo("Y Axis Quantity", QuantityLabel(state.params.var2))) {
    for (int q = 0; q < partblock.nAllQ; ++q) {
      QuantityId cand = partblock.allQ[q];
      bool is_selected = (cand == state.params.var2);
      if (ImGui::Selectable(QuantityLabel(cand), is_selected))
        state.params.var2 = cand;
      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  ImGui::InputInt("Bins X", &state.params.bins1);
  ImGui::InputInt("Bins Y", &state.params.bins2);

  ImGui::Checkbox("Use Log scale X", &state.params.logScaleX);
  ImGui::Checkbox("Use Log scale Y", &state.params.logScaleY);
  ImGui::Checkbox("Use Log color scale", &state.params.logScaleColor);

  ImGui::Checkbox("Auto Range", &state.params.autoRange);

  if (!state.params.autoRange) {
    ImGui::InputFloat("X Axis Min", &state.params.range1_min, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("X Axis Max", &state.params.range1_max, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Min", &state.params.range2_min, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Max", &state.params.range2_max, 0.0f, 0.0f, "%g");
  }

#ifdef USE_CONVEX_HULL
  ImGui::Checkbox("Filter: Use Convex Hull", &state.params.useConvexHull);
#endif

  ImGui::Checkbox("Filter: Use Camera Center", &state.params.useCameraCenter);
  if (state.params.useCameraCenter) {
    ImGui::InputFloat("Camera Radius", &state.params.cameraRadius, 0.1f, 1.0f, "%.2f");
  }

  if (ImGui::Button("Compute Histogram")) {
    state.result = computer.compute(partblock, state.params, ctx);
    if (state.result.valid) {
      state.computed = true;

      state.params.range1_min = state.result.range1_min;
      state.params.range1_max = state.result.range1_max;
      state.params.range2_min = state.result.range2_min;
      state.params.range2_max = state.result.range2_max;
    }
  }

  if (state.computed && state.result.valid) {
    size_t computedBins1 = state.result.values.size();
    size_t computedBins2 = (computedBins1 > 0) ? state.result.values[0].size() : 0;

    TrackingVector<float> heatmapData;
    heatmapData.reserve(computedBins1 * computedBins2);

    for (size_t j = 0; j < computedBins2; j++) {
      for (size_t i = 0; i < computedBins1; i++) {
        heatmapData.push_back(state.result.values[i][j]);
      }
    }

    if (ImPlot::BeginPlot("2D Histogram", ImVec2(-1, 300))) {
      ImPlot::SetupAxes(QuantityLabel(state.params.var1), QuantityLabel(state.params.var2));

      ImPlot::SetupAxisLimits(ImAxis_X1,
                              state.params.range1_min,
                              state.params.range1_max,
                              ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1,
                              state.params.range2_min,
                              state.params.range2_max,
                              ImGuiCond_Always);

      ImPlot::PushColormap(ImPlotColormap_Viridis);

      ImPlot::PlotHeatmap("Histogram",
                          heatmapData.data(),
                          (int)computedBins2,
                          (int)computedBins1,
                          0, 0, "",
                          ImPlotPoint(state.params.range1_min, state.params.range2_min),
                          ImPlotPoint(state.params.range1_max, state.params.range2_max));

      ImPlot::EndPlot();
    }
  }

  ImGui::End();
}


static void InitProjectionPreviewFonts(ProjectionMapGenerator& generator,
                                       ProjectionMapUIState& state)
{
  if (state.previewFontsInitialized) return;

  state.previewFonts.clear();

  ImGui::GetIO().Fonts->AddFontDefault();

  for (int i = 0; i < generator.getFontCount(); ++i) {
    ImFont* font =
      ImGui::GetIO().Fonts->AddFontFromFileTTF(generator.getFontPath(i).c_str(), 24.0f);
    state.previewFonts.push_back(font);
  }

  ImGui::GetIO().Fonts->Build();
  state.previewFontsInitialized = true;
}

static void DrawProjectionFontSelectionUI(ProjectionMapGenerator& generator,
                                          ProjectionMapUIState& state)
{
  if (!state.fontWindowOpen) return;

  ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_Appearing);
  ImGui::Begin("Font Selection Preview", &state.fontWindowOpen, ImGuiWindowFlags_None);

  if (generator.getFontCount() == 0) {
    ImGui::Text("No fonts available.");
    ImGui::End();
    return;
  }

  InitProjectionPreviewFonts(generator, state);

  if (state.currentFontIndex >= generator.getFontCount()) {
    state.currentFontIndex = 0;
  }

  if (ImGui::BeginCombo("Select Font",
                        generator.getFontPath(state.currentFontIndex).c_str())) {
    for (int i = 0; i < generator.getFontCount(); i++) {
      bool isSelected = (state.currentFontIndex == i);
      if (ImGui::Selectable(generator.getFontPath(i).c_str(), isSelected)) {
        state.currentFontIndex = i;
        generator.selectFontFileByIndex(i);
      }
      if (isSelected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  ImGui::Text("Selected Font: %s",
              generator.getFontPath(state.currentFontIndex).c_str());

  if (state.currentFontIndex < (int)state.previewFonts.size() &&
      state.previewFonts[state.currentFontIndex]) {
    ImGui::PushFont(state.previewFonts[state.currentFontIndex]);
    ImGui::Text("The quick brown fox jumps over the lazy dog.");
    ImGui::PopFont();
  }

  ImGui::End();
}

namespace {
  void updateInteractiveCuboidIfNeeded(CuboidObject& cuboid,
				       RenderLayerState& cuboidAnnotationState,
				       const glm::vec3& newCenter,
				       const glm::quat& newOrientation,
				       const glm::vec3& newHalfSize,
				       bool newShow)
  {
    bool changed = false;

    if (glm::distance(cuboid.center, newCenter) > 1e-6f) {
      cuboid.center = newCenter;
      changed = true;
    }

    if (glm::length(cuboid.orientation - newOrientation) > 1e-6f) {
      cuboid.orientation = glm::normalize(newOrientation);
      changed = true;
    }

    if (glm::distance(cuboid.halfSize, newHalfSize) > 1e-6f) {
      cuboid.halfSize = newHalfSize;
      changed = true;
    }

    if (cuboidAnnotationState.show != newShow) {
      cuboidAnnotationState.show = newShow;
      changed = true;
    }

    if (changed) {
      cuboidAnnotationState.cpuUpdated = true;
    }
  }
}

void OpenProjectionMapUI() {
  gProjectionMapUIState.open = true;
}

void DrawProjectionMapUI(ProjectionMapGenerator& generator,
                         ParticleArray* P,
                         CameraContext& camCtx,
			 RenderLayerState& cuboidAnnotationState,
                         int indexfile)
{
  auto& state = gProjectionMapUIState;
  if (!state.open) return;

  auto& params = generator.params;
  auto& cuboid = generator.interactiveCuboid();
  int prevSelectedAxis = params.selectedAxis; 
  
  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
  ImGui::Begin("make projectoin map", &state.open, ImGuiWindowFlags_None);

  generator.originalMax = P->originalMax;
  generator.desiredMax  = P->desiredMax;

  // -----------------------------
  // side length
  // -----------------------------
  ImGui::Checkbox("use original coordinate for side length", &state.useOriginalCoordinate);
  ImGui::InputFloat3("side lenght", state.xlen_input);

  if (state.useOriginalCoordinate) {
    for (int k = 0; k < 3; k++) {
      params.xlen[k] = state.xlen_input[k] * generator.desiredMax / generator.originalMax;
    }
  } else {
    for (int k = 0; k < 3; k++) {
      params.xlen[k] = state.xlen_input[k];
    }
  }

  ImGui::InputFloat3("offset center", params.xoffset);
  ImGui::InputInt("npixel", &params.npixel, 10, 1000);
  ImGui::InputFloat3("Plane Tilt (deg) x", params.tilt);

  if (ImGui::Button("move center to camera pos")) {
    for (int k = 0; k < 3; k++) {
      params.xoffset[k] = camCtx.cameraTarget[k];
    }
  }

  // params -> derived state
  generator.center = glm::vec3(params.xoffset[0], params.xoffset[1], params.xoffset[2]);

  float xmin[3], xmax[3];
  for (int k = 0; k < 3; k++) {
    xmin[k] = params.xoffset[k] - 0.5f * params.xlen[k];
    xmax[k] = params.xoffset[k] + 0.5f * params.xlen[k];
  }

  generator.cuboidTransform = generator.UpdateTransformFromEuler(params.tilt);
  ImGui::Checkbox("show cubic region", &params.flagShowCuboid);

  // -----------------------------
  // mouse drag region selection
  // -----------------------------
  camCtx.stopCameraMode = state.selectMode;

  if (state.selectMode) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
  }

  if (ImGui::Button(state.selectMode ? "Exit Region Select" : "Select Region (Mouse Drag)")) {
    state.selectMode = !state.selectMode;
    printf("center_init=%g %g %g\n",
           generator.center.x, generator.center.y, generator.center.z);
  }
  ImGui::PopStyleColor();

  glm::vec3 pivot(generator.center.x, generator.center.y, generator.center.z);

  if (state.selectMode) {
    glm::mat4 view = glm::lookAt(camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp);

    ImGuiIO& io = ImGui::GetIO();

    float xpos = io.MousePos.x;
    float ypos = io.MousePos.y;
    static float lastX = xpos;
    static float lastY = ypos;

    float dx = xpos - lastX;
    float dy = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    if (ImGui::IsMouseDown(0)) {
      ImGuiIO& io = ImGui::GetIO();
            
      UpdateCuboidTransformArcball(cuboid,
				   xpos - dx, ypos - dy,
				   lastX, lastY,
				   io.DisplaySize.x, io.DisplaySize.y,
				   view, pivot);

      cuboidAnnotationState.cpuUpdated = true;
    }

    glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(cuboid.orientation));
    params.tilt[0] = eulerAngles.x;
    params.tilt[1] = eulerAngles.y;
    params.tilt[2] = eulerAngles.z;

    generator.center = cuboid.center;
    generator.cuboidTransform = cuboid.orientation;
  } else {
    generator.cuboidTransform = generator.UpdateTransformFromEuler(params.tilt);
    generator.center = glm::vec3(params.xoffset[0], params.xoffset[1], params.xoffset[2]);
  }

  // -----------------------------
  // angular momentum axis
  // -----------------------------
  if (ImGui::Button("set axis from angular momentum")) {
    glm::vec3 normal(0.f, 0.f, 1.f);
    generator.planeNormal = glm::normalize(
					   generator.calc_angular_momentum_axis(P->particleBlock.particles,
										generator.center,
										params.xlen));

    printf("planeNormal = %g %g %g\n",
           generator.planeNormal.x,
           generator.planeNormal.y,
           generator.planeNormal.z);

    glm::vec3 v = glm::cross(normal, generator.planeNormal);
    float w = 1.0f + glm::dot(normal, generator.planeNormal);
    generator.cuboidTransform = glm::normalize(glm::quat(w, v.x, v.y, v.z));

    glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(generator.cuboidTransform));
    params.tilt[0] = eulerAngles.x;
    params.tilt[1] = eulerAngles.y;
    params.tilt[2] = eulerAngles.z;
  }

  updateInteractiveCuboidIfNeeded(cuboid,
				  cuboidAnnotationState,
				  generator.center,
				  generator.cuboidTransform,
				  glm::vec3(0.5f * params.xlen[0],
					    0.5f * params.xlen[1],
					    0.5f * params.xlen[2]),
				  params.flagShowCuboid);
    
  cuboid.edgeColor = glm::vec4(1.0f);
  cuboid.tag = "interactive_cuboid";
  
  // -----------------------------
  // output file
  // -----------------------------
  ImGui::InputText("File Format", params.fileFormat, IM_ARRAYSIZE(params.fileFormat));
  ImGui::InputText("Folder", params.folderPath, IM_ARRAYSIZE(params.folderPath));

  char filename[512];
  snprintf(filename, sizeof(filename), "%s/%s", params.folderPath, params.fileFormat);
  snprintf(filename, sizeof(filename), filename, indexfile);

  // -----------------------------
  // projection axis
  // -----------------------------
  const char* axisLabels[] = {
    "X-Axis (YZ Plane)",
    "Y-Axis (XZ Plane)",
    "Z-Axis (XY Plane)"
  };
  ImGui::Combo("Projection Normal Axis",
               &params.selectedAxis,
               axisLabels,
               IM_ARRAYSIZE(axisLabels));

  glm::vec3 normal(0, 0, 1);
  if (params.selectedAxis == 0) normal = glm::vec3(1, 0, 0);
  if (params.selectedAxis == 1) normal = glm::vec3(0, 1, 0);
  if (params.selectedAxis == 2) normal = glm::vec3(0, 0, 1);

  if (prevSelectedAxis != params.selectedAxis) 
    cuboidAnnotationState.cpuUpdated = true;  

  // -----------------------------
  // particle type
  // -----------------------------
  const char* typeLabels[] = { "0", "1", "2", "3", "4", "5" };
  ImGui::Combo("SelectedParticleType",
               &params.selectedType,
               typeLabels,
               IM_ARRAYSIZE(typeLabels));

  // type から dataSource を自然に補助決定
  if (params.selectedType == 0) {
    params.dataSource = DataSource::Gas;
  } else if (params.selectedType == 1 || params.selectedType == 2) {
    params.dataSource = DataSource::DM;
  } else {
    params.dataSource = DataSource::Stars;
  }

  generator.planeNormal = glm::normalize(generator.cuboidTransform * normal);

  // -----------------------------
  // colormap
  // -----------------------------
  if (params.colormapindex < 0) params.colormapindex = 0;
  if (params.colormapindex >= gNumColormaps) params.colormapindex = gNumColormaps - 1;
  
  const char* previewName = gColormapDefs[params.colormapindex].name;
  if (ImGui::BeginCombo("Colormap##projection", previewName)) {
    for (int i = 0; i < gNumColormaps; ++i) {
      bool selected = (params.colormapindex == i);
      if (ImGui::Selectable(gColormapDefs[i].name, selected)) {
	params.colormapindex = i;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  
  generator.colorMap      = gColormapDefs[params.colormapindex].data;
  generator.countColorMap = gColormapDefs[params.colormapindex].count;
    
  // -----------------------------
  // data source specific UI
  // -----------------------------
  if (params.dataSource == DataSource::Gas) {
    if (ImGui::BeginCombo("Quantity", QuantityLabel(params.selectedVarGas))) {
      for (int q = 0; q < P->particleBlock.nUIQ; ++q) {
        QuantityId cand = P->particleBlock.uiQ[q];
        bool is_selected = (cand == params.selectedVarGas);
        if (ImGui::Selectable(QuantityLabel(cand), is_selected)) {
          params.selectedVarGas = cand;
          params.var = QuantityLabel(cand);
        }
        if (is_selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    ImGui::Checkbox("Density Weighting", &params.flagDensityWeight);
    ImGui::Checkbox("Use Voronoi tesselation", &params.flagVoronoi);

    if (params.flagVoronoi) {
      ImGui::SetNextItemWidth(200.0f);
      ImGui::SameLine();
      ImGui::InputInt("nz", &params.step_z, 10, 1000);
    }
  }
  else if (params.dataSource == DataSource::Stars) {
    params.flagDensityWeight = false;
    params.flagVoronoi = false;

    const char* quantities[] = { "Density", "Metallicity", "Mass", "Flux" };
    int q = static_cast<int>(params.starQuantity);
    if (ImGui::Combo("Quantity", &q, quantities, IM_ARRAYSIZE(quantities))) {
      params.starQuantity = static_cast<StarQuantity>(q);

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
      ImGui::InputFloat("Band center (nm)",
                        &params.flux.band_center_nm,
                        10.0f, 100.0f, "%.1f");
      ImGui::InputFloat("Band width  (nm)",
                        &params.flux.band_width_nm,
                        10.0f, 100.0f, "%.1f");
      params.flux.band_width_nm = std::max(params.flux.band_width_nm, 1.0f);
    }

    ImGui::SliderFloat("Gaussian sigma (pixels)",
                       &params.psf_sigma_pix,
                       0.3f, 10.0f, "%.2f");
  }
  else {
    // DM
    params.flagDensityWeight = false;
    params.flagVoronoi = false;
    params.var = "DM projection";
  }

  // -----------------------------
  // color scale / range
  // -----------------------------
  ImGui::Checkbox("Use Log color scale", &params.flagLogScale);
  ImGui::Checkbox("Auto Range", &params.autoRange);

  if (!params.autoRange) {
    ImGui::Indent();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputFloat("Min", &params.range_min, 0.0f, 0.0f, "%g");

    ImGui::SetNextItemWidth(100.0f);
    ImGui::SameLine();
    ImGui::InputFloat("Max", &params.range_max, 0.0f, 0.0f, "%g");
    ImGui::Unindent();
  }

  // -----------------------------
  // time label
  // -----------------------------
  ImGui::Checkbox("Show time label", &params.flagTimeLabel);
  if (params.flagTimeLabel) {
    ImGui::Indent();
    ImGui::InputText("Time Format",
                     params.timeFormatBuf,
                     IM_ARRAYSIZE(params.timeFormatBuf));

    ImGui::SameLine();
    ImGui::Checkbox("Use redshift", &params.flagUseRedshift);

    ImGui::InputFloat("Time unit to display", &params.factorShownTimeInUnitTime);
    ImGui::Unindent();
  }

  // -----------------------------
  // scale bar
  // -----------------------------
  ImGui::Checkbox("Show Spacial scale", &params.flagPlaceScale);
  if (params.flagPlaceScale) {
    ImGui::Indent();
    ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("arrow size", &params.arrowLenX);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputText("arrow label",
                     params.arrowLabelStr,
                     sizeof(params.arrowLabelStr));

    ImGui::SameLine();
    ImGui::Checkbox("Use original coordinate",
                    &params.flagScaleOriginalCoordinate);
    ImGui::Unindent();
  }

  // -----------------------------
  // zoom region
  // -----------------------------
  ImGui::Checkbox("Rescale center to zoom-in region",
                  &params.flagSpecifyZoomRegionByMass);
  if (params.flagSpecifyZoomRegionByMass) {
    ImGui::Indent();
    ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("critical mass",
                      &params.criticalGasMassForZoomRegion,
                      0.0f, 0.0f, "%g");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("size of zoom-in region",
                      &params.lenZoomRegion,
                      0.0f, 0.0f, "%g");

    ImGui::SameLine();
    ImGui::Checkbox("Use original coordinate",
                    &params.flagScaleOriginalCoordinateZoomRegion);
    ImGui::Unindent();
  }

  // -----------------------------
  // font selection
  // -----------------------------
  if (ImGui::Button("Select font")) {
    generator.showWindowSelectFont = true;
  }

  DrawProjectionFontSelectionUI(generator, state);

  // -----------------------------
  // star particle overlay
  // -----------------------------
  ImGui::Checkbox("draw star particle", &params.flagShowStarParticles);

#ifdef USE_LUA
  if (generator.flag_init_lua == false) {
    generator.gLua = luaL_newstate();
    luaL_openlibs(generator.gLua);
    generator.flag_init_lua = true;
  }
#endif

  if (params.flagShowStarParticles) {
    ImGui::InputText("Filter Expression",
                     params.filterExpr,
                     IM_ARRAYSIZE(params.filterExpr));
    ImGui::InputText("Point Size Expression",
                     params.pointSizeExpr,
                     IM_ARRAYSIZE(params.pointSizeExpr));
    ImGui::InputText("Point Color Expression",
                     params.pointColorExpr,
                     IM_ARRAYSIZE(params.pointColorExpr));
    ImGui::InputText("Min Value Expression",
                     params.minValueExpr,
                     IM_ARRAYSIZE(params.minValueExpr));
    ImGui::InputText("Max Value Expression",
                     params.maxValueExpr,
                     IM_ARRAYSIZE(params.maxValueExpr));
  }

  // -----------------------------
  // render
  // -----------------------------
  if (ImGui::Button("render 2D projection map")) {
    if (params.selectedType == 0) {
      params.dataSource = DataSource::Gas;
    } else if (params.selectedType == 1 || params.selectedType == 2) {
      params.dataSource = DataSource::DM;
    } else {
      params.dataSource = DataSource::Stars;
    }
    
    generator.make_density_map(P, filename);
  }

  ImGui::End();
}


void DrawTopParticlesUI(ParticleArray* P, CameraContext& camCtx) {
  auto& state = gTopParticlesUIState;
  const int histSizeMax = 10;

  ImGui::Begin("Particles Info");

  ImGui::InputInt("Particle ID", &state.queryID);
  ImGui::SameLine();

  if (ImGui::Button("Show Info")) {
    state.hasFound = false;
    for (const auto& p : P->particleBlock.particles) {
      if (p.ID == state.queryID) {
        state.foundParticle = p;
        state.hasFound = true;
        break;
      }
    }

    if (state.hasFound) {
      state.historyData.push_front(state.foundParticle);
      if ((int)state.historyData.size() > histSizeMax)
        state.historyData.pop_back();
      state.historySel = 0;
    }
  }

  if (!state.hasFound && state.queryID >= 0) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Particle ID %d not found", state.queryID);
  }

  if (ImGui::Button("Refresh History")) {
    int prevID = (state.historySel >= 0 && state.historySel < (int)state.historyData.size())
                   ? state.historyData[state.historySel].ID
                   : -1;

    std::deque<ParticleData> newHistory;
    std::unordered_set<int> seen;

    for (const auto& oldP : state.historyData) {
      if (seen.find(oldP.ID) != seen.end())
        continue;

      auto it = std::find_if(
        P->particleBlock.particles.begin(), P->particleBlock.particles.end(),
        [&](const ParticleData& p) { return p.ID == oldP.ID; });

      if (it != P->particleBlock.particles.end()) {
        newHistory.push_back(*it);
        seen.insert(oldP.ID);
      }
    }

    state.historyData.swap(newHistory);

    if (prevID >= 0) {
      state.historySel = -1;
      for (int i = 0; i < (int)state.historyData.size(); ++i) {
        if (state.historyData[i].ID == prevID) {
          state.historySel = i;
          break;
        }
      }
    }

    if (state.historySel == -1 && !state.historyData.empty())
      state.historySel = 0;
  }

  ImGui::SameLine();

  if (ImGui::Button("Clear History")) {
    state.historyData.clear();
    state.historySel = -1;
  }

  for (size_t i = 0; i < state.historyData.size(); i++) {
    auto& p = state.historyData[i];

    char label[512];
    std::snprintf(label, sizeof(label),
                  "ID %d: mass = %.3g, pos = (%.2g, %.2g, %.2g), vel = (%.2g, %.2g, %.2g), r=%g rho=%g T=%g H=%g",
                  p.ID, p.mass * (P->UnitMass_in_msolar / P->Hubble),
                  p.pos[0], p.pos[1], p.pos[2],
                  p.vel[0], p.vel[1], p.vel[2],
                  p.originalHsml, p.density, p.temperature, P->Hubble);

    if (ImGui::Selectable(label, state.historySel == (int)i)) {
      float distance = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
      glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
      camCtx.cameraTarget = glm::vec3(p.pos[0], p.pos[1], p.pos[2]);
      camCtx.cameraPos = camCtx.cameraTarget - direction * distance;
      state.historySel = (int)i;
    }
  }

  if (ImGui::Button("Center this paritcle")) {
    if (state.hasFound && state.historySel >= 0) {
      P->flag_follow_particle_ID = true;
      P->TargetParticleID = state.historyData[state.historySel].ID;
      P->flag_follow_clump_center = false;
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Disable center paritcle")) {
    P->flag_follow_particle_ID = false;
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
    state.filtered.clear();

    for (size_t i = 0; i < P->particleBlock.particles.size(); i++) {
      const ParticleData& p = P->particleBlock.particles[i];
      if (P->flag_mask[i] != 0)
        continue;

      if (p.type >= 0 && p.type < 6 && state.selectType[p.type])
        state.filtered.push_back(p);
    }

    std::sort(state.filtered.begin(), state.filtered.end(),
              [](const ParticleData& a, const ParticleData& b) {
                return a.mass > b.mass;
              });
  }

  int count = std::min(state.m, (int)state.filtered.size());

  ImGui::Text("Type %d : Showing top %d particles sorted by mass", state.particleType, count);
  for (int i = 0; i < count; i++) {
    char label[512];
    std::snprintf(label, sizeof(label),
                  "ID %d: mass = %.3g, pos = (%.2g, %.2g, %.2g) vel = (%.2g, %.2g, %.2g), radius = %g rho=%g t=%g",
                  state.filtered[i].ID, state.filtered[i].mass * (P->UnitMass_in_msolar / P->Hubble),
                  state.filtered[i].pos[0], state.filtered[i].pos[1], state.filtered[i].pos[2],
                  state.filtered[i].vel[0], state.filtered[i].vel[1], state.filtered[i].vel[2],
                  state.filtered[i].originalHsml,
                  state.filtered[i].density, state.filtered[i].temperature);

    if (ImGui::Selectable(label)) {
      float distance = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
      glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
      camCtx.cameraTarget = glm::vec3(state.filtered[i].pos[0], state.filtered[i].pos[1], state.filtered[i].pos[2]);
      camCtx.cameraPos = camCtx.cameraTarget - direction * distance;
    }
  }

  ImGui::Text("Plot 1d histogram");

  const char* quantities[] = { "x", "y", "z", "r", "Density", "Temperature", "Hsml", "Mass" };
  ImGui::Combo("Quantity", &state.selectedVar, quantities, IM_ARRAYSIZE(quantities));
  std::string var = quantities[state.selectedVar];

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
    std::function<bool(const ParticleData&)> func = [](const ParticleData&) { return true; };

    if (state.useCameraCenter) {
      glm::vec3 camCenter = camCtx.cameraTarget;
      float radius = state.cameraRadius;

      auto isWithinRadius = [camCenter, radius](const ParticleData& p) -> bool {
        glm::vec3 pos(p.pos[0], p.pos[1], p.pos[2]);
        return glm::length(pos - camCenter) <= radius;
      };

      auto prevFunc = func;
      func = [prevFunc, isWithinRadius](const ParticleData& p) -> bool {
        return prevFunc(p) && isWithinRadius(p);
      };
    }

    float massMin = std::numeric_limits<float>::max();
    float massMax = std::numeric_limits<float>::lowest();

    for (const auto& p : state.filtered) {
      float value = p.getValue(var);
      value *= (P->UnitMass_in_msolar / P->Hubble);

      if (value == 0.0f) continue;
      if (!func(p)) continue;

      if (state.histogramLogScaleX)
        value = log10(value);

      massMin = std::min(massMin, value);
      massMax = std::max(massMax, value);
    }

    if (massMin == massMax)
      massMax = massMin + 1.0f;

    if (state.autoRange) {
      state.range1_min = massMin;
      state.range1_max = massMax;
    }

    TrackingVector<int> binCounts(state.bins, 0);
    state.binSize = (state.range1_max - state.range1_min) / state.bins;

    for (const auto& p : state.filtered) {
      float value = p.getValue(var);
      value *= (P->UnitMass_in_msolar / P->Hubble);

      if (value == 0.0f) continue;
      if (!func(p)) continue;

      if (state.histogramLogScaleX)
        value = log10(value);

      int bin = static_cast<int>((value - state.range1_min) / state.binSize);
      if (bin < 0) bin = 0;
      if (bin >= state.bins) bin = state.bins - 1;
      binCounts[bin]++;
    }

    state.histBins.resize(state.bins);
    state.binCenters.resize(state.bins);

    state.vmin = std::numeric_limits<float>::max();
    state.vmax = std::numeric_limits<float>::lowest();

    for (int i = 0; i < state.bins; i++) {
      state.histBins[i] = static_cast<float>(binCounts[i]);

      float value = state.histBins[i];
      if (state.histogramLogScaleY) {
        if (value == 0.0f)
          continue;
        value = log10(value);
      }

      state.vmin = std::min(state.vmin, value);
      state.vmax = std::max(state.vmax, value);
    }

    if (state.histogramLogScaleY) {
      state.vmin = std::floor(state.vmin);
      state.vmax = std::ceil(state.vmax);
      state.vmin = 0.8f * std::pow(10.0f, state.vmin);
      state.vmax = std::pow(10.0f, state.vmax);
    } else {
      state.vmin = 0.0f;
      int digits = (state.vmax > 0.0f) ? static_cast<int>(log10(state.vmax)) : 0;
      double scale = std::pow(10.0, digits);
      state.vmax = std::ceil(state.vmax / scale) * scale;
    }

    if (state.autoRange) {
      state.range2_min = state.vmin;
      state.range2_max = state.vmax;
    }

    for (int i = 0; i < state.bins; i++)
      state.binCenters[i] = state.range1_min + (i + 0.5f) * state.binSize;

    state.histogramComputed = true;
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
                       state.bins,
                       state.binSize);

      ImPlot::EndPlot();
    }
  }

  ImGui::End();
}

void OpenHaloesUI() {
  gHaloesUIState.open = true;
}

void DrawHaloesUI(ParticleArray* P, CameraContext& camCtx, FileInfo* fileInfo)
{
#ifdef HAVE_HDF5
  auto& state = gHaloesUIState;
  if (!state.open) return;
  
  ImGui::SetNextWindowSize(ImVec2(980, 620), ImGuiCond_Appearing);
  ImGui::Begin("Halo lists", &state.open, ImGuiWindowFlags_None);

  ImGui::InputText("Filename", state.fname, IM_ARRAYSIZE(state.fname));

  {
    if (ImGui::Button("Load halo catalog (no IDs)")) {
      auto cat = fileInfo->readHaloCatalogFromHDF5(state.fname, /*loadIDs=*/false);
      P->Haloes = std::move(cat.haloes);

      P->haloIDs.clear();
      P->haloIDsLoaded = false;
      P->haloChecked.assign(P->Haloes.size(), 0);
    }

    ImGui::SameLine();

    if (ImGui::Button("Load halo catalog (+ IDs)")) {
      auto cat = fileInfo->readHaloCatalogFromHDF5(state.fname, /*loadIDs=*/true);
      P->Haloes  = std::move(cat.haloes);
      P->haloIDs = std::move(cat.haloIDs);

      P->haloIDsLoaded = (!P->haloIDs.empty() && P->haloIDs.size() == P->Haloes.size());
      P->haloChecked.assign(P->Haloes.size(), 0);

      P->particleBlock.id2indexDirty = true;
    }
  }

  if (P->Haloes.empty()) {
    ImGui::TextUnformatted("No haloes loaded.");
    ImGui::End();
    return;
  }

  ImGui::InputInt("Number of Halo list", &state.m);
  if (state.m < 1) state.m = 1;
  const int count = std::min(state.m, (int)P->Haloes.size());
  ImGui::SameLine();
  ImGui::Text(" / %zu halos", P->Haloes.size());

  if (!P->haloIDsLoaded) {
    ImGui::TextUnformatted("Halo particle IDs: not loaded. (Checkbox needs IDs)");
  } else {
    ImGui::TextUnformatted("Halo particle IDs: loaded.");
    ImGui::SameLine();
    if (ImGui::Button("Clear IDs")) {
      P->haloIDs.clear();
      P->haloChecked.assign(P->Haloes.size(), 0);
      P->haloIDsLoaded = false;
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset ALL flag_stress")) {
      for (auto& p : P->particleBlock.particles) p.flag_stress = 0;
      P->particlesDirty = true;
      std::fill(P->haloChecked.begin(), P->haloChecked.end(), 0);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Recompute halo center from member particles:");

    ImGui::Checkbox("Mass-weighted", &state.recomputeUseMassWeight);
    ImGui::SameLine();
    ImGui::Checkbox("Use original_pos (recommended)", &state.recomputeUseOriginalPos);

    ImGui::InputInt("Min N particles", &state.recomputeMinParticles);
    if (state.recomputeMinParticles < 1) state.recomputeMinParticles = 1;

    if (ImGui::Button("Recompute halo positions from particle distribution")) {
      P->recomputeHaloPositionsFromParticles(state.recomputeUseMassWeight,
                                             state.recomputeUseOriginalPos,
                                             state.recomputeMinParticles);
    }
  }

  ImGui::Separator();

  auto focusCameraOnHaloPos = [&](const HaloData& h) {
    float distance = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
    glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);

    float pos[3];
    pos[0] = h.GroupPos[0] * P->desiredMax / P->originalMax;
    pos[1] = h.GroupPos[1] * P->desiredMax / P->originalMax;
    pos[2] = h.GroupPos[2] * P->desiredMax / P->originalMax;

    camCtx.cameraTarget = glm::vec3(pos[0], pos[1], pos[2]);
    camCtx.cameraPos    = camCtx.cameraTarget - direction * distance;
  };

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
      const HaloData& h = P->Haloes[i];

      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      bool checked = (P->haloIDsLoaded && i < (int)P->haloChecked.size()) ? (P->haloChecked[i] != 0) : false;

      ImGui::PushID(i);
      if (ImGui::Checkbox("##haloStress", &checked)) {
        if (!P->haloIDsLoaded) {
          checked = false;
        } else {
          P->haloChecked[i] = checked ? 1 : 0;
          P->ApplyHaloStress(i, checked);
        }
      }
      ImGui::PopID();

      ImGui::TableSetColumnIndex(1);
      ImGui::PushID(i + 100000);
      if (ImGui::Selectable(std::to_string(i).c_str(), false,
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
        focusCameraOnHaloPos(h);
      }
      ImGui::PopID();

      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%.3g", h.GroupMass);

      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%d", h.GroupLen);

      ImGui::TableSetColumnIndex(4);
      ImGui::Text("%.3g", h.GroupMassType[0]);

      ImGui::TableSetColumnIndex(5);
      double sm = (double)h.GroupMassType[3] + h.GroupMassType[4] + h.GroupMassType[5];
      ImGui::Text("%.3g", (float)sm);

      ImGui::TableSetColumnIndex(6);
      ImGui::Text("(%.2g, %.2g, %.2g)", h.GroupPos[0], h.GroupPos[1], h.GroupPos[2]);

      ImGui::TableSetColumnIndex(7);
      ImGui::Text("(%.2g, %.2g, %.2g)", h.GroupVel[0], h.GroupVel[1], h.GroupVel[2]);

      ImGui::TableSetColumnIndex(8);
      ImGui::Text("%.3g / %.3g", h.GroupMetallicity[0], h.GroupMetallicity[1]);
    }

    ImGui::EndTable();
  }

  ImGui::Separator();

  ImGui::Text("Plot halo histogram");

  const char* quantities[] = {
    "Mass", "GasMass", "StellarMass", "GasMetallicity", "StellarMetallicity"
  };
  ImGui::Combo("Quantity", &state.selectedVar, quantities, IM_ARRAYSIZE(quantities));
  std::string var = quantities[state.selectedVar];

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
    float massMin = std::numeric_limits<float>::max();
    float massMax = std::numeric_limits<float>::lowest();

    for (const auto& p : P->Haloes) {
      float mass = p.getHaloValue(var);
      if (state.histogramLogScaleX)
        mass = log10(mass);

      massMin = std::min(massMin, mass);
      massMax = std::max(massMax, mass);
    }

    if (massMin == massMax)
      massMax = massMin + 1.0f;

    if (state.autoRange) {
      state.range1_min = massMin;
      state.range1_max = massMax;
    }

    TrackingVector<int> binCounts(state.bins, 0);
    state.binSize = (state.range1_max - state.range1_min) / state.bins;

    for (const auto& p : P->Haloes) {
      float mass = p.getHaloValue(var);
      if (state.histogramLogScaleX)
        mass = log10(mass);

      int bin = static_cast<int>((mass - state.range1_min) / state.binSize);
      if (bin < 0) bin = 0;
      if (bin >= state.bins) bin = state.bins - 1;
      binCounts[bin]++;
    }

    state.vmin = std::numeric_limits<float>::max();
    state.vmax = std::numeric_limits<float>::lowest();

    state.histBins.resize(state.bins);
    state.binCenters.resize(state.bins);

    for (int i = 0; i < state.bins; i++) {
      state.histBins[i] = static_cast<float>(binCounts[i]);

      float value = state.histBins[i];
      if (state.histogramLogScaleY) {
        if (value == 0.0f) continue;
        value = log10(value);
      }

      state.vmin = std::min(state.vmin, value);
      state.vmax = std::max(state.vmax, value);
    }

    if (state.histogramLogScaleY) {
      state.vmin = std::floor(state.vmin);
      state.vmax = std::ceil(state.vmax);
      state.vmin = 0.8f * std::pow(10.0f, state.vmin);
      state.vmax = std::pow(10.0f, state.vmax);
    } else {
      state.vmin = 0.0f;
      int digits = (state.vmax > 0.0f) ? static_cast<int>(log10(state.vmax)) : 0;
      double scale = std::pow(10.0, digits);
      state.vmax = std::ceil(state.vmax / scale) * scale;
    }

    if (state.autoRange) {
      state.range2_min = state.vmin;
      state.range2_max = state.vmax;
    }

    for (int i = 0; i < state.bins; i++)
      state.binCenters[i] = state.range1_min + (i + 0.5f) * state.binSize;

    state.histogramComputed = true;
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
                       state.bins,
                       state.binSize);
      ImPlot::EndPlot();
    }
  }

  ImGui::End();
#endif
}

void OpenMaskUI() {
  gMaskUIState.open = true;
}

bool DrawMaskWindow() {
  if (!gMaskUIState.open) return false;

  bool changed = false;
  bool apply = false;

  // ★第二引数に &ui.showWindow を渡すと × で閉じられる
  if (!ImGui::Begin("Mask Settings", &gMaskUIState.open)) {
    ImGui::End();
    return false;
  }

  changed |= ImGui::Checkbox("Enable Sphere", &gMaskUIState.enableSphere);
  changed |= ImGui::DragFloat3("Center", gMaskUIState.center, 0.1f);
  changed |= ImGui::DragFloat("Radius", &gMaskUIState.radius, 0.1f, 0.0f, 1e30f);

  int om = (int)gMaskUIState.outsideMode;
  changed |= ImGui::Combo("Outside Mode", &om, "Drop\0Thin\0KeepAll\0");
  gMaskUIState.outsideMode = (MaskUIState::OutsideMode)om;

  if (gMaskUIState.outsideMode == MaskUIState::OutsideMode::Thin) {
    changed |= ImGui::DragInt("Outside Stride", &gMaskUIState.outsideStride, 1.0f, 1, 1000000);
  }

  ImGui::Separator();
  ImGui::Text("Particle Type Policy");
  const char* typeNames[6] = {"Gas(0)","DM(1)","Type2","Type3","Star(4)","BH(5)"};
  for (int t=0; t<6; ++t) {
    int tm = (int)gMaskUIState.typeMode[t];
    ImGui::PushID(t);
    changed |= ImGui::Combo(typeNames[t], &tm, "Off\0On (NoThin)\0On (ThinOK)\0");
    gMaskUIState.typeMode[t] = (MaskUIState::TypeMode)tm;
    ImGui::PopID();
  }

  ImGui::Separator();
  changed |= ImGui::Checkbox("Enable Max Particles (ID thinning)", &gMaskUIState.enableMaxParticles);
  if (gMaskUIState.enableMaxParticles) {
    changed |= ImGui::DragInt("Max Particles", &gMaskUIState.maxParticles, 1000.0f, 1, 1000000000);
  }

  ImGui::Separator();
  changed |= ImGui::Checkbox("Auto Apply", &gMaskUIState.autoApply);

  // Apply/Close ボタン
  if (ImGui::Button("Apply")) {
    gMaskUIState.revision++;
    apply = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Close")) {
    gMaskUIState.open = false; // 明示的に閉じる
  }

  ImGui::End();

  if (!apply && changed && gMaskUIState.autoApply) {
    gMaskUIState.revision++;
    apply = true;
  }
  return apply; // ★「設定が反映された」トリガとして true
}

MaskConfig MakeMaskConfigFromUI(){
  MaskConfig cfg;
  cfg.enableSphere = gMaskUIState.enableSphere;
  cfg.center[0] = gMaskUIState.center[0];
  cfg.center[1] = gMaskUIState.center[1];
  cfg.center[2] = gMaskUIState.center[2];
  cfg.radius = (double)gMaskUIState.radius;

  cfg.outsideStride = (uint64_t)std::max(1, gMaskUIState.outsideStride);
  switch(gMaskUIState.outsideMode){
    case MaskUIState::OutsideMode::Drop:    cfg.outsideMode = MaskConfig::OutsideMode::Drop; break;
    case MaskUIState::OutsideMode::Thin:    cfg.outsideMode = MaskConfig::OutsideMode::Thin; break;
    case MaskUIState::OutsideMode::KeepAll: cfg.outsideMode = MaskConfig::OutsideMode::KeepAll; break;
  }

  for(int t=0;t<6;t++){
    switch(gMaskUIState.typeMode[t]){
      case MaskUIState::TypeMode::Off:      cfg.typePolicy[t] = MaskConfig::ThinPolicy::ReadOff; break;
      case MaskUIState::TypeMode::On_NoThin:cfg.typePolicy[t] = MaskConfig::ThinPolicy::ReadOn_NoThin; break;
      case MaskUIState::TypeMode::On_ThinOK:cfg.typePolicy[t] = MaskConfig::ThinPolicy::ReadOn_ThinOK; break;
    }
  }

  cfg.enableMaxParticles = gMaskUIState.enableMaxParticles;
  cfg.maxParticles = (gMaskUIState.enableMaxParticles && gMaskUIState.maxParticles>0) ? (size_t)gMaskUIState.maxParticles : 0;

  return cfg;
}

void ExportMaskConfigState(ConfigMaskState& outState)
{
  outState = ConfigMaskState{};
  outState.valid = true;

  outState.enableSphere = gMaskUIState.enableSphere;
  outState.center[0] = gMaskUIState.center[0];
  outState.center[1] = gMaskUIState.center[1];
  outState.center[2] = gMaskUIState.center[2];
  outState.radius = gMaskUIState.radius;

  switch (gMaskUIState.outsideMode) {
    case MaskUIState::OutsideMode::Drop:
      outState.outsideMode = ConfigMaskState::OutsideMode::Drop; break;
    case MaskUIState::OutsideMode::Thin:
      outState.outsideMode = ConfigMaskState::OutsideMode::Thin; break;
    case MaskUIState::OutsideMode::KeepAll:
      outState.outsideMode = ConfigMaskState::OutsideMode::KeepAll; break;
  }

  outState.outsideStride = (unsigned long long)std::max(1, gMaskUIState.outsideStride);

  for (int t = 0; t < 6; ++t) {
    switch (gMaskUIState.typeMode[t]) {
      case MaskUIState::TypeMode::Off:
        outState.typeMode[t] = ConfigMaskState::TypeMode::Off; break;
      case MaskUIState::TypeMode::On_NoThin:
        outState.typeMode[t] = ConfigMaskState::TypeMode::On_NoThin; break;
      case MaskUIState::TypeMode::On_ThinOK:
        outState.typeMode[t] = ConfigMaskState::TypeMode::On_ThinOK; break;
    }
  }

  outState.enableMaxParticles = gMaskUIState.enableMaxParticles;
  outState.maxParticles = gMaskUIState.maxParticles;
}

void ApplyMaskConfigState(const ConfigMaskState& state)
{
  if (!state.valid) return;
  
  gMaskUIState.enableSphere = state.enableSphere;
  gMaskUIState.center[0] = (float)state.center[0];
  gMaskUIState.center[1] = (float)state.center[1];
  gMaskUIState.center[2] = (float)state.center[2];
  gMaskUIState.radius = (float)state.radius;

  switch (state.outsideMode) {
    case ConfigMaskState::OutsideMode::Drop:
      gMaskUIState.outsideMode = MaskUIState::OutsideMode::Drop; break;
    case ConfigMaskState::OutsideMode::Thin:
      gMaskUIState.outsideMode = MaskUIState::OutsideMode::Thin; break;
    case ConfigMaskState::OutsideMode::KeepAll:
      gMaskUIState.outsideMode = MaskUIState::OutsideMode::KeepAll; break;
  }

  gMaskUIState.outsideStride = (int)std::max<unsigned long long>(1, state.outsideStride);

  for (int t = 0; t < 6; ++t) {
    switch (state.typeMode[t]) {
      case ConfigMaskState::TypeMode::Off:
        gMaskUIState.typeMode[t] = MaskUIState::TypeMode::Off; break;
      case ConfigMaskState::TypeMode::On_NoThin:
        gMaskUIState.typeMode[t] = MaskUIState::TypeMode::On_NoThin; break;
      case ConfigMaskState::TypeMode::On_ThinOK:
        gMaskUIState.typeMode[t] = MaskUIState::TypeMode::On_ThinOK; break;
    }
  }

  gMaskUIState.enableMaxParticles = state.enableMaxParticles;
  gMaskUIState.maxParticles = state.maxParticles;

  // 読み込み後に自動反映したいなら
  gMaskUIState.revision++;
}

void DrawProjectionPreviewUI(const ProjectionMapGenerator& gen,
                             const ProjectionPreviewUIState& st)
{
  if (!gen.getImageFlag()) return;

  ImGui::Begin("2D Projection Map");
  if (st.valid) {
    ImGui::Image((ImTextureID)st.textureId, ImVec2((float)st.width, (float)st.height));
  }
  ImGui::End();
}

void ColorbarLabelRenderer::draw(const ColorbarGizmoState& gizmo) const
{
  if (!gizmo.visible) return;

  ImGuiIO& io = ImGui::GetIO();
  float scaleX = io.DisplayFramebufferScale.x;
  float scaleY = io.DisplayFramebufferScale.y;

  ImDrawList* draw_list = ImGui::GetForegroundDrawList();
  if (!draw_list) return;

  const auto& layout = gizmo.layout;
  const int numTicks = gizmo.content.numTicks;

  for (int i = 0; i < numTicks; i++) {
    float t = (numTicks > 1) ? float(i) / float(numTicks - 1) : 0.0f;
    float value = gizmo.content.valueMin + t * (gizmo.content.valueMax - gizmo.content.valueMin);

    float px_phys = layout.left_pixel + t * (layout.right_pixel - layout.left_pixel);
    float py_phys = layout.bottom_pixel + 5.0f * scaleY;

    float sx = (px_phys + layout.offsetX) / scaleX;
    float sy = (py_phys + layout.offsetY) / scaleY;

    float draw_x = std::floor(sx + 0.5f);
    float draw_y = std::floor(sy + 0.5f);

    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", value);

    draw_list->AddText(ImVec2(draw_x, draw_y),
                       IM_COL32(255,255,255,255),
                       buf);
  }
}

void ShowTime(double time){
    // 画面の左上（ピクセル座標 (10,10)）にウィンドウを固定する
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    // 背景を透明にしたい場合
    ImGui::SetNextWindowBgAlpha(0.3f);
    // ウィンドウフラグでタイトルバーや枠、スクロールバーを非表示にする
    ImGui::Begin("Time Overlay", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoSavedSettings);
    // glfwGetTime() を用いて経過時間を表示（必要に応じてフォーマットを変更してください）
    ImGui::Text("Time: %.4f", time);
    ImGui::End();
}

void ShowCameraSettingsUI(){};
