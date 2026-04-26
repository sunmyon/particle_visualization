#include "common_ui.h"

#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "app/runtime_state.h"
#include "render/gizmo_renderer.h"

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

void ShowTime(const SnapshotCurrentState& current){
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
    if (current.useComovingCoordinates) {
      ImGui::Text("a: %.6g", current.loadedScaleFactor);
      ImGui::Text("z: %.6g", current.loadedRedshift);
      if (current.hasCosmicTime) {
        ImGui::Text("cosmic time [Gyr]: %.6g", current.loadedCosmicTime);
      } else {
        ImGui::Text("cosmic time: N/A");
      }
    } else {
      ImGui::Text("time: %.6g", current.loadedTime);
    }
    ImGui::End();
}

void ShowCameraSettingsUI(){};
