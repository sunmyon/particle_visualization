#include <imgui.h>
#include "implot.h"

#include "FindClumps/clump_chain.h"
#include "FindClumps/clump_window_state.h"

#include "FileIO/snapshot_source.h"
#include "FileIO/file_io.h"

#include "app/normalization_config.h"
#include "app/input_filter_config.h"
#include "data/particle_array.h"
#include "interaction/camera.h"

#include "projection/make_2D_projection_map.h"

void OpenClumpChainUI(ClumpChainWindowState& state){
  state.open = true;
}

static void DrawClumpChainTableSection(ClumpChainWindowState& ui, ClumpChain& chain);

static void DrawSelectedClumpChainNavigation(ClumpChainWindowState& ui,
					     ClumpChain& chain,
                                             ParticleArray* P,
                                             FileInfo& fileinfo,
                                             CameraContext& cam,
                                             NormalizationContext& normalization,
                                             const InputFilterConfig& filter,
                                             const SnapshotSource& src);

static void DrawSelectedClumpChainPlot(ClumpChainWindowState& ui,
				       ClumpChain& chain,
                                       ParticleArray* P);

static void DrawSelectedClumpProjectionSection(ClumpChainWindowState& ui,
					       ClumpChain& chain,
                                               ParticleArray* P,
                                               ProjectionMapGenerator* proj,
                                               FileInfo& fileinfo,
                                               NormalizationContext& normalization,
                                               const InputFilterConfig& filter,
                                               const SnapshotSource& src);

static void DrawSelectedClumpChainSection(ClumpChainWindowState& ui,
					  ClumpChain& chain,
                                          ParticleArray* P,
                                          ProjectionMapGenerator* proj,
                                          FileInfo& fileinfo,
                                          CameraContext& cam,
                                          NormalizationContext& normalization,
                                          const InputFilterConfig& filter,
                                          const SnapshotSource& src);


static void DrawVerticalDashedLine(double x_value, const ImU32& col, float thickness, float dash_length, float gap_length);

void DrawClumpChainListUI(ClumpChainWindowState& ui,
			  ClumpChain& chain,
			  ParticleArray* P,
			  ProjectionMapGenerator* proj,
			  FileInfo& fileinfo,
			  CameraContext& cam,
			  NormalizationContext& normalization,
			  const InputFilterConfig& filter)
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
    DrawSelectedClumpChainSection(ui, chain, P, proj, fileinfo, cam, normalization, filter, src);
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

static void DrawSelectedClumpChainNavigation(ClumpChainWindowState& ui,
					     ClumpChain& chain,
                                             ParticleArray* P,
                                             FileInfo& fileinfo,
                                             CameraContext& cam,
                                             NormalizationContext& normalization,
                                             const InputFilterConfig& filter,
                                             const SnapshotSource& src)
{
  ImGui::BeginDisabled(ui.selectedChainIndex == -1);

  const auto& chainProps = chain.props();
  const auto& clumpChain = chain.chains();
  
  if (ui.selectedChainIndex >= 0 &&
      ui.selectedChainIndex < static_cast<int>(chainProps.size())) {
    const auto& selected_chain = chainProps[ui.selectedChainIndex];
    const auto& ch = clumpChain[ui.selectedChainIndex];

    bool flag_button_pushed = false;
    if (ImGui::Button("Load Selected Chain")) {
      ui.currentSnapshotIndex = 0;
      ui.flagFileLoaded = true;
      flag_button_pushed = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Prev")) {
      if (ui.flagFileLoaded && ui.currentSnapshotIndex > 0) {
        ui.currentSnapshotIndex--;
        flag_button_pushed = true;
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Next")) {
      if (ui.flagFileLoaded && ui.currentSnapshotIndex < static_cast<int>(ch.size()) - 1) {
        ui.currentSnapshotIndex++;
        flag_button_pushed = true;
      }
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
                P->particleBlock.header.time);

    if (flag_button_pushed) {
      int snapshot = src.initialIndex + (selected_chain.first_snapshot + ui.currentSnapshotIndex) * src.skipStep;

      float pos[3];
      float scale_from_phys = normalization.toNormalizedScale();
      pos[0] = ch[ui.currentSnapshotIndex]->pos[0] * scale_from_phys;
      pos[1] = ch[ui.currentSnapshotIndex]->pos[1] * scale_from_phys;
      pos[2] = ch[ui.currentSnapshotIndex]->pos[2] * scale_from_phys;

      fileinfo.loadNewSnapshot(snapshot, P, normalization, filter);

      float dist = glm::length(cam.cameraPos - cam.cameraTarget);
      glm::vec3 direction = cam.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
      cam.cameraTarget = glm::vec3(pos[0], pos[1], pos[2]);
      cam.cameraPos = cam.cameraTarget - direction * dist;

      flag_button_pushed = false;
    }
  }

  ImGui::EndDisabled();
}

static void DrawSelectedClumpChainPlot(ClumpChainWindowState& ui,
				       ClumpChain& chain,
                                       ParticleArray* P)
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

    double currentTime = P->particleBlock.header.time;
    ImU32 red = ImGui::GetColorU32(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    DrawVerticalDashedLine(currentTime, red, 1.0f, 5.0f, 3.0f);

    ImPlot::EndPlot();
  }
}

static void DrawSelectedClumpProjectionSection(ClumpChainWindowState& ui,
					       ClumpChain& chain,
                                               ParticleArray* P,
                                               ProjectionMapGenerator* proj,
                                               FileInfo& fileinfo,
                                               NormalizationContext& normalization,
                                               const InputFilterConfig& filter,
                                               const SnapshotSource& src)
{
  const auto& selected_chain = chain.prop(ui.selectedChainIndex);
  const auto& ch = chain.chain(ui.selectedChainIndex);

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
  std::string var = quantities2[ui.selectedProjectionVar];

  if (ImGui::Button("make projection maps")) {
    for (size_t i = 0; i < ch.size(); i++) {
      int flag_use_amvector = (i == 0) ? 1 : 0;

      int snapshot = src.initialIndex + (selected_chain.first_snapshot + static_cast<int>(i)) * src.skipStep;
      fileinfo.loadNewSnapshot(snapshot, P, normalization, filter);

      float pos_center[3];
      float scale_from_phys = normalization.toNormalizedScale();
      pos_center[0] = ch[i]->pos[0] * scale_from_phys;
      pos_center[1] = ch[i]->pos[1] * scale_from_phys;
      pos_center[2] = ch[i]->pos[2] * scale_from_phys;

      char fname_output[512];
      std::snprintf(fname_output, sizeof(fname_output),
                    "%s/image_clump%d_%04zu.png",
                    ui.mapOutputDir,
                    ui.selectedChainIndex,
                    i);

      proj->set_projection_parameters(P->particleBlock.particles,
                                      flag_use_amvector,
                                      pos_center,
                                      ui.mapLen,
                                      ui.mapValMin,
                                      ui.mapValMax,
                                      ui.mapNpixel,
                                      ui.mapNslices,
                                      var);
      proj->make_density_map(P, fname_output);
    }
  }
}


static void DrawSelectedClumpChainSection(ClumpChainWindowState& ui,
					  ClumpChain& chain,
                                          ParticleArray* P,
                                          ProjectionMapGenerator* proj,
                                          FileInfo& fileinfo,
                                          CameraContext& cam,
                                          NormalizationContext& normalization,
                                          const InputFilterConfig& filter,
                                          const SnapshotSource& src)
{
  DrawSelectedClumpChainNavigation(ui, chain, P, fileinfo, cam, normalization, filter, src);
  DrawSelectedClumpChainPlot(ui, chain, P);
  DrawSelectedClumpProjectionSection(ui, chain, P, proj, fileinfo, normalization, filter, src);
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
