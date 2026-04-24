#include <imgui.h>
#include "implot.h"

#include "FindClumps/clump_chain.h"
#include "FindClumps/clump_window_state.h"

#include "FileIO/snapshot_source.h"
#include "FileIO/file_io.h"

#include "app/runtime_state.h"
#include "app/normalization_config.h"
#include "data/particle_array.h"
#include "interaction/camera.h"

#include "projection/make_2D_projection_map.h"
#include "projection/projection_geometry.h"
#include "projection/projection_map_context.h"
#include "projection/projection_map_params.h"
#include "image/image_io.h"
#include "image/rgb_image.h"

void OpenClumpChainUI(ClumpChainWindowState& state){
  state.open = true;
}

static void DrawClumpChainTableSection(ClumpChainWindowState& ui, ClumpChain& chain);

static void DrawSelectedClumpChainNavigation(ClumpChainWindowState& ui,
					     ClumpChain& chain,
                                             ParticleArray* P,
					     HeaderInfo& header,
                                             SnapshotLoadRuntimeState& snapshotLoad,
                                             CameraContext& cam,
                                             NormalizationContext& normalization,
                                             const SnapshotSource& src);

static void DrawSelectedClumpChainPlot(ClumpChainWindowState& ui,
				       ClumpChain& chain,
				       double time);

static void DrawSelectedClumpProjectionSection(ClumpChainWindowState& ui,
					       ClumpChain& chain,
                                               ParticleArray* P,
					       HeaderInfo& header,
                                               ProjectionMapGenerator* proj,
                                               const ProjectionMapParams& baseParams,
                                               SnapshotLoadRuntimeState& snapshotLoad,
                                               NormalizationContext& normalization,
                                               const SnapshotSource& src);

static void DrawSelectedClumpChainSection(ClumpChainWindowState& ui,
					  ClumpChain& chain,
                                          ParticleArray* P,
					  HeaderInfo& header,
                                          ProjectionMapGenerator* proj,
                                          const ProjectionMapParams& baseParams,
                                          SnapshotLoadRuntimeState& snapshotLoad,
                                          CameraContext& cam,
                                          NormalizationContext& normalization,
                                          const SnapshotSource& src);


static void DrawVerticalDashedLine(double x_value, const ImU32& col, float thickness, float dash_length, float gap_length);

void DrawClumpChainListUI(ClumpChainWindowState& ui,
			  ClumpChain& chain,
			  ParticleArray* P,
			  HeaderInfo& header,
			  ProjectionMapGenerator* proj,
			  const ProjectionMapParams& baseParams,
			  FileInfo& fileinfo,			 
			  SnapshotLoadRuntimeState& snapshotLoad,
			  CameraContext& cam,
			  NormalizationContext& normalization)
{
  if (!ui.open)
    return;

  const auto& src = fileinfo.getSource();

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
  ImGui::Begin("Clump chain lists", &ui.open, ImGuiWindowFlags_None);

  if (ImGui::Button("extract clump evolution chain")) {
    chain.build(ui.clumpChainInitFileIndex,
		ui.clumpChainNsnapshots,
		ui.clumpChainDFileIndex,
		ui.clumpChainFileName,
		P->units);
  }

  if(chain.computed()){
    DrawClumpChainTableSection(ui, chain);
    DrawSelectedClumpChainSection(ui, chain, P, header, proj, baseParams, snapshotLoad, cam, normalization, src);
  }

  ImGui::End();
}


static void DrawClumpChainTableSection(ClumpChainWindowState& ui, ClumpChain& chain)
{
  const auto& chainProps = chain.props();

  chain.ensurePlotSize();
  auto& chainPlot = chain.plot();

  if (!ImGui::BeginTable("ClumpChainTable", 10, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    return;

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
 
  for (size_t idx = 0; idx < chainProps.size(); idx++) {
    auto& ch = chainProps[idx];

    ImGui::TableNextRow();
    ImGui::PushID(static_cast<int>(idx));

    bool is_selected = (static_cast<int>(idx) == ui.selectedChainIndex);

    ImGui::TableSetColumnIndex(0);
    if (ImGui::Selectable(("Chain " + std::to_string(ch.global_id)).c_str(),
                          is_selected,
                          ImGuiSelectableFlags_SpanAllColumns)) {
      ui.selectedChainIndex = static_cast<int>(idx);
    }

    if (is_selected) {
      ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                             ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.8f, 0.5f)));
    }

    ImGui::TableSetColumnIndex(1); ImGui::Text("%d", ch.first_snapshot);
    ImGui::TableSetColumnIndex(2); ImGui::Text("%d", ch.last_snapshot);
    ImGui::TableSetColumnIndex(3); ImGui::Text("%d", ch.stellar_id);
    ImGui::TableSetColumnIndex(4); ImGui::Text("%g", ch.mstar);
    ImGui::TableSetColumnIndex(5); ImGui::Text("%d", ch.nstar);
    ImGui::TableSetColumnIndex(6); ImGui::Text("%g", ch.mstar_maximum);
    ImGui::TableSetColumnIndex(7); ImGui::Text("%g", ch.mass_maximum);
    ImGui::TableSetColumnIndex(8); ImGui::Text("%g", ch.temperature_d);

    ImGui::TableSetColumnIndex(9);
    bool flag = chainPlot[idx];
    if (ImGui::Checkbox(("##plot" + std::to_string(idx)).c_str(), &flag)) {
      chainPlot[idx] = flag;
    }

    ImGui::PopID();
  }

  ImGui::EndTable();
}

static void RecenterCameraPreservingDistance(CameraContext& cam, const glm::vec3& newTarget)
{
  const float dist = glm::length(cam.cameraPos - cam.cameraTarget);
  const glm::vec3 direction = cam.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
  cam.cameraTarget = newTarget;
  cam.cameraPos = cam.cameraTarget - direction * dist;
}

static void DrawSelectedClumpChainNavigation(ClumpChainWindowState& ui,
					     ClumpChain& chain,
                                             ParticleArray* P,
					     HeaderInfo& header,
                                             SnapshotLoadRuntimeState& snapshotLoad,
                                             CameraContext& cam,
                                             NormalizationContext& normalization,
                                             const SnapshotSource& src)
{
  (void)P;

  if (ui.navigationLoadPending &&
      IsSnapshotLoadedFor(snapshotLoad,
                          SnapshotLoadOwner::UserNavigation,
                          ui.navigationPendingStep)) {
    RecenterCameraPreservingDistance(cam,
                                     glm::vec3(ui.navigationPendingCenter[0],
                                               ui.navigationPendingCenter[1],
                                               ui.navigationPendingCenter[2]));
    ui.navigationLoadPending = false;
  }

  ImGui::BeginDisabled(ui.selectedChainIndex == -1);

  const auto& chainProps = chain.props();
  const auto& clumpChain = chain.chains();
  
  if (ui.selectedChainIndex >= 0 &&
      ui.selectedChainIndex < static_cast<int>(chainProps.size())) {
    const auto& selected_chain = chainProps[ui.selectedChainIndex];
    const auto& ch = clumpChain[ui.selectedChainIndex];

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
      cam.cameraPos = cam.cameraTarget + glm::vec3(0.0f, 0.0f, -1.0f);

      glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
      glm::vec3 forward = glm::normalize(cam.cameraTarget - cam.cameraPos);
      glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
      glm::vec3 up = glm::normalize(glm::cross(right, forward));

      glm::mat4 viewMatrix = glm::lookAt(cam.cameraPos, cam.cameraTarget, up);
      glm::mat3 rotationMatrix = glm::mat3(viewMatrix);
      cam.distance = glm::length(cam.cameraPos - cam.cameraTarget);
	      cam.cameraOrientation = glm::quat_cast(rotationMatrix);
	    }

    ImGui::Text("current snapshot index: %d (init=%d now=%d step=%d) time=%g",
                src.initialIndex + (selected_chain.first_snapshot + ui.currentSnapshotIndex) * src.skipStep,
                selected_chain.first_snapshot,
                selected_chain.first_snapshot + ui.currentSnapshotIndex,
                src.skipStep,
                header.time);

    bool triggerLoad = false;
    if (ui.requestLoadSelected) {
      ui.currentSnapshotIndex = 0;
      ui.flagFileLoaded = true;
      triggerLoad = true;
      ui.requestLoadSelected = false;
    }

    if (ui.requestPrev) {
      if (ui.flagFileLoaded && ui.currentSnapshotIndex > 0) {
        ui.currentSnapshotIndex--;
        triggerLoad = true;
      }
      ui.requestPrev = false;
    }

    if (ui.requestNext) {
      if (ui.flagFileLoaded &&
          ui.currentSnapshotIndex < static_cast<int>(ch.size()) - 1) {
        ui.currentSnapshotIndex++;
        triggerLoad = true;
      }
      ui.requestNext = false;
    }

    if (triggerLoad) {
      const int targetStep = selected_chain.first_snapshot + ui.currentSnapshotIndex;
      const float scaleFromPhys = normalization.toNormalizedScale();
      ui.navigationPendingCenter[0] = ch[ui.currentSnapshotIndex]->pos[0] * scaleFromPhys;
      ui.navigationPendingCenter[1] = ch[ui.currentSnapshotIndex]->pos[1] * scaleFromPhys;
      ui.navigationPendingCenter[2] = ch[ui.currentSnapshotIndex]->pos[2] * scaleFromPhys;
      ui.navigationPendingStep = targetStep;
      ui.navigationLoadPending = true;

      RequestSnapshotLoad(snapshotLoad,
                          SnapshotLoadOwner::UserNavigation,
                          targetStep,
                          100);
    }
  }

  ImGui::EndDisabled();
}

static void DrawSelectedClumpChainPlot(ClumpChainWindowState& ui,
				       ClumpChain& chain,
				       double time)
{
  const auto& chainProps = chain.props();
  const auto& chainPlot  = chain.plot();
  const auto& clumpChain = chain.chains();  
  
  if (ui.selectedChainIndex < 0 ||
      ui.selectedChainIndex >= static_cast<int>(chainProps.size()))
    return;
  
  const char* quantities[] = { "Density", "Temperature", "ClumpMass", "StellarMass" };
  ImGui::Combo("Quantity", &ui.selectedVar, quantities, IM_ARRAYSIZE(quantities));
  std::string var = quantities[ui.selectedVar];

  ImGui::Checkbox("Use Log scale Y", &ui.useLogScaleY);
  ImGui::Checkbox("Use autoscale", &ui.autoScale);

  if (!ui.autoScale) {
    ImGui::InputFloat("X Axis Min", &ui.xmin, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("X Axis Max", &ui.xmax, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Min", &ui.ymin, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Max", &ui.ymax, 0.0f, 0.0f, "%g");
  }

  if (ImPlot::BeginPlot("Time Evolution", ImVec2(-1, 300), ImPlotFlags_None)) {
    ImPlot::SetupAxis(ImAxis_X1, "Time", ImPlotAxisFlags_None);
    ImPlot::SetupAxis(ImAxis_Y1, var.c_str(), ImPlotAxisFlags_None);

    if (ui.useLogScaleY)
      ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
    else
      ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);

    TrackingVector<TrackingVector<float>> times_array, values_array;
    for (size_t i = 0; i < chainProps.size(); i++) {
      if (chainPlot[i] || static_cast<int>(i) == ui.selectedChainIndex) {
        const auto& ch = clumpChain[i];

        TrackingVector<float> times, values;
        for (const auto& snap : ch) {
          times.push_back(snap->time);
          values.push_back(snap->getValue(var));
        }

        times_array.push_back(times);
        values_array.push_back(values);
      }
    }

    if (ui.autoScale) {
      float time_max = -1.e20f, value_max = -1.e20f;
      float time_min =  1.e20f, value_min =  1.e20f;

      for (size_t i = 0; i < times_array.size(); i++) {
        auto& times = times_array[i];
        auto& values = values_array[i];

        for (size_t j = 0; j < times.size(); j++) {
          if (time_max < times[j]) time_max = times[j];
          if (time_min > times[j]) time_min = times[j];

          if (ui.useLogScaleY) {
            if (values[j] > 0.0f) {
              if (value_max < values[j]) value_max = values[j];
              if (value_min > values[j]) value_min = values[j];
            }
          } else {
            if (value_max < values[j]) value_max = values[j];
            if (value_min > values[j]) value_min = values[j];
          }
        }
      }

      ui.xmax = time_max;
      ui.xmin = time_min;
      ui.ymax = value_max;
      ui.ymin = value_min;
    }

    ImPlot::SetupAxisLimits(ImAxis_X1, ui.xmin, ui.xmax, ImGuiCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, ui.ymin, ui.ymax, ImGuiCond_Always);

    for (size_t i = 0; i < times_array.size(); i++) {
      auto& times = times_array[i];
      auto& values = values_array[i];
      ImPlot::PlotLine(var.c_str(), times.data(), values.data(), static_cast<int>(times.size()));
    }

    double currentTime = time;
    ImU32 red = ImGui::GetColorU32(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    DrawVerticalDashedLine(currentTime, red, 1.0f, 5.0f, 3.0f);

    ImPlot::EndPlot();
  }
}

static void DrawSelectedClumpProjectionSection(ClumpChainWindowState& ui,
					       ClumpChain& chain,
                                               ParticleArray* P,
					       HeaderInfo& header,
                                               ProjectionMapGenerator* proj,
                                               const ProjectionMapParams& baseParams,
                                               SnapshotLoadRuntimeState& snapshotLoad,
                                               NormalizationContext& normalization,
                                               const SnapshotSource& src)
{
  (void)src;

  ImGui::PushItemWidth(100);
  ImGui::InputFloat("len", &ui.mapLen, 0.0f, 0.0f, "%g");
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

  if (ui.requestMakeProjectionMaps && !ui.projectionBatchRunning) {
    if (ui.selectedChainIndex >= 0 &&
        ui.selectedChainIndex < static_cast<int>(chain.props().size())) {
      ui.projectionBatchRunning = true;
      ui.projectionBatchCursor = 0;
      ui.projectionBatchChainIndex = ui.selectedChainIndex;
    }
    ui.requestMakeProjectionMaps = false;
  }

  if (!ui.projectionBatchRunning) {
    return;
  }

  ImGui::SameLine();
  ImGui::Text("running: %d", ui.projectionBatchCursor);

  if (ui.projectionBatchChainIndex < 0 ||
      ui.projectionBatchChainIndex >= static_cast<int>(chain.props().size())) {
    ui.projectionBatchRunning = false;
    return;
  }

  const auto& selected_chain = chain.prop(ui.projectionBatchChainIndex);
  const auto& clumpSnapshots = chain.chain(ui.projectionBatchChainIndex);

  if (ui.projectionBatchCursor >= static_cast<int>(clumpSnapshots.size())) {
    ui.projectionBatchRunning = false;
    return;
  }

  const int targetStep = selected_chain.first_snapshot + ui.projectionBatchCursor;
  if (!IsSnapshotLoadedFor(snapshotLoad,
                          SnapshotLoadOwner::ClumpChainProjectionBatch,
                          targetStep)) {
    RequestSnapshotLoad(snapshotLoad,
                        SnapshotLoadOwner::ClumpChainProjectionBatch,
                        targetStep,
                        20);
    return;
  }

  const QuantityId projectionVars[] = {
    QuantityId::Density,
    QuantityId::Temperature,
    QuantityId::Val,
    QuantityId::Val2,
    QuantityId::Hsml,
    QuantityId::Mass
  };
  const bool useAngularMomentumAxis = (ui.projectionBatchCursor == 0);

  float pos_center[3];
  const float scale_from_phys = normalization.toNormalizedScale();
  pos_center[0] = clumpSnapshots[ui.projectionBatchCursor]->pos[0] * scale_from_phys;
  pos_center[1] = clumpSnapshots[ui.projectionBatchCursor]->pos[1] * scale_from_phys;
  pos_center[2] = clumpSnapshots[ui.projectionBatchCursor]->pos[2] * scale_from_phys;

  char fname_output[512];
  std::snprintf(fname_output, sizeof(fname_output),
                "%s/image_clump%d_%04d.png",
                ui.mapOutputDir,
                ui.projectionBatchChainIndex,
                ui.projectionBatchCursor);

  ProjectionMapParams frameParams = baseParams;
  frameParams.dataSource = DataSource::Gas;
  frameParams.selectedType = 0;
  frameParams.selectedVarGas = projectionVars[ui.selectedProjectionVar];
  frameParams.var = QuantityLabel(frameParams.selectedVarGas);
  frameParams.xoffset[0] = pos_center[0];
  frameParams.xoffset[1] = pos_center[1];
  frameParams.xoffset[2] = pos_center[2];
  frameParams.xlen[0] = ui.mapLen;
  frameParams.xlen[1] = ui.mapLen;
  frameParams.xlen[2] = ui.mapLen;
  frameParams.range_min = ui.mapValMin;
  frameParams.range_max = ui.mapValMax;
  frameParams.autoRange = false;
  frameParams.npixel = ui.mapNpixel;
  frameParams.step_z = ui.mapNslices;
  frameParams.flagVoronoi = (ui.mapNslices > 1);

  ProjectionMapContext context =
    BuildProjectionMapContext(frameParams,
                              normalization.toPhysicalScale(),
                              header.time);

  if (useAngularMomentumAxis) {
    auto frame = ComputeAngularMomentumFrame(P->particleBlock.particles,
                                             context.center,
                                             frameParams.xlen);
    if (frame.valid) {
      context.center = frame.center;
      context.planeNormal = frame.axis;
      context.cuboidTransform = BuildRotationFromZAxisTo(frame.axis);
    }
  }

  RgbImage image = proj->makeDensityMapImage(*P, frameParams, context);
  if (!WritePngRgb(fname_output, image.width, image.height, image.rgb)) {
    std::fprintf(stderr, "Failed to write projection map: %s\n", fname_output);
  }

  ++ui.projectionBatchCursor;
  if (ui.projectionBatchCursor >= static_cast<int>(clumpSnapshots.size())) {
    ui.projectionBatchRunning = false;
  } else {
    const int nextStep = selected_chain.first_snapshot + ui.projectionBatchCursor;
    RequestSnapshotLoad(snapshotLoad,
                        SnapshotLoadOwner::ClumpChainProjectionBatch,
                        nextStep,
                        20);
  }
}


static void DrawSelectedClumpChainSection(ClumpChainWindowState& ui,
					  ClumpChain& chain,
                                          ParticleArray* P,
					  HeaderInfo& header,
                                          ProjectionMapGenerator* proj,
                                          const ProjectionMapParams& baseParams,
                                          SnapshotLoadRuntimeState& snapshotLoad,
                                          CameraContext& cam,
                                          NormalizationContext& normalization,
                                          const SnapshotSource& src)
{
  DrawSelectedClumpChainNavigation(ui, chain, P, header, snapshotLoad, cam, normalization, src);
  DrawSelectedClumpChainPlot(ui, chain, header.time);
  DrawSelectedClumpProjectionSection(ui, chain, P, header, proj, baseParams, snapshotLoad, normalization, src);
}
      

static void DrawVerticalDashedLine(double x_value, const ImU32& col, float thickness, float dash_length, float gap_length) {
    // プロット内の Y 軸の表示範囲を取得
    ImPlotRect limits = ImPlot::GetPlotLimits();
    double y_min = limits.Y.Min;
    double y_max = limits.Y.Max;

    // データ空間での端点 (x_value, y_min) と (x_value, y_max) をピクセル座標に変換
    ImVec2 p0 = ImPlot::PlotToPixels(ImVec2(x_value, y_min));
    ImVec2 p1 = ImPlot::PlotToPixels(ImVec2(x_value, y_max));

    // x 座標は固定
    float x_pixel = p0.x;
    
    // 垂直方向のピクセル距離を計算（p1.y > p0.y と仮定）
    float total_length = p1.y - p0.y;    
    if(total_length < 0){
      float tmp = p0.y;
      p0.y = p1.y;
      p1.y = tmp;
      total_length = p1.y - p0.y;    
    }

    ImDrawList* draw_list = ImPlot::GetPlotDrawList();
    if (!draw_list) return; 
    
    float current_y = p0.y;    
    while (current_y < p1.y) {
      float seg_end = current_y + dash_length;
      if (seg_end > p1.y)
	seg_end = p1.y;

      draw_list->AddLine(ImVec2(x_pixel, current_y), ImVec2(x_pixel, seg_end), col, thickness);
      current_y += dash_length + gap_length;
    }
}
