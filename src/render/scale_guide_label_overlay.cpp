#include "render/scale_guide_label_overlay.h"

#include <cmath>
#include <vector>

#include <imgui.h>
#include <glm/glm.hpp>

#include "interaction/camera.h"
#include "render/render_viewport.h"

namespace {
bool ProjectToScreen(const glm::vec3& worldPos,
                     const CameraContext& camera,
                     const glm::mat4& view,
                     const glm::mat4& proj,
                     const RenderViewport& viewport,
                     glm::vec2& screen,
                     bool requireInsideViewport)
{
  (void)camera;
  const glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.0f);
  if (clip.w <= 0.0f) {
    return false;
  }

  const glm::vec3 ndc = glm::vec3(clip) / clip.w;
  if (requireInsideViewport &&
      (std::abs(ndc.x) > 1.0f || std::abs(ndc.y) > 1.0f)) {
    return false;
  }

  screen = NdcToImGui(viewport, ndc);
  return true;
}

bool RectsOverlap(const ImVec4& a, const ImVec4& b)
{
  return a.x < b.z && a.z > b.x && a.y < b.w && a.w > b.y;
}
}

void ScaleGuideLabelOverlay::draw(const CameraContext& camera,
                                  const glm::mat4& view,
                                  const glm::mat4& proj,
                                  const RenderViewport& viewport) const
{
  if (labels_.empty()) {
    return;
  }

  ImDrawList* draw = ImGui::GetBackgroundDrawList();
  if (!draw) {
    return;
  }

  std::vector<ImVec4> occupiedRects;
  occupiedRects.reserve(labels_.size());

  for (const ScaleGuideLabelItem& item : labels_) {
    if (item.text.empty()) {
      continue;
    }

    glm::vec2 screen(0.0f);
    if (!ProjectToScreen(item.worldPos,
                         camera,
                         view,
                         proj,
                         viewport,
                         screen,
                         true)) {
      continue;
    }

    if (item.usePixelDistanceCull) {
      glm::vec2 anchorScreen(0.0f);
      if (!ProjectToScreen(item.anchorWorldPos,
                           camera,
                           view,
                           proj,
                           viewport,
                           anchorScreen,
                           false)) {
        continue;
      }
      if (glm::length(screen - anchorScreen) < item.minPixelDistance) {
        continue;
      }
    }

    const char* text = item.text.c_str();
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 pos(screen.x + 4.0f, screen.y + 4.0f);
    const ImVec4 rect(pos.x - 2.0f,
                      pos.y - 2.0f,
                      pos.x + textSize.x + 2.0f,
                      pos.y + ImGui::GetFontSize() + 2.0f);

    bool overlaps = false;
    for (const ImVec4& occupied : occupiedRects) {
      if (RectsOverlap(rect, occupied)) {
        overlaps = true;
        break;
      }
    }
    if (overlaps) {
      continue;
    }

    draw->AddRectFilled(ImVec2(pos.x - 2.0f, pos.y - 2.0f),
                        ImVec2(pos.x + textSize.x + 2.0f,
                               pos.y + ImGui::GetFontSize() + 2.0f),
                        IM_COL32(0, 0, 0, 128),
                        2.0f);
    draw->AddText(pos, IM_COL32_WHITE, text);
    occupiedRects.push_back(rect);
  }
}
