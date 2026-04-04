#include "particle_label_overlay.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include <imgui.h>
#include <glm/glm.hpp>

#include "interaction/camera.h"
#include "window_context.h"
#include "ui_state.h"
#include "FileIO/file_io.h"

void ParticleLabelOverlay::updateIfNeeded(ParticleArray& P,
                                          const CameraContext& camCtx,
                                          SettingsRuntimeState& settings)
{
  if (P.flagParticleIndexDirty == false) {
    if (glm::distance(camCtx.cameraPos, settings.lastCameraPos) < settings.moveThreshold)
      return;
  }

  settings.lastCameraPos = camCtx.cameraPos;
  labels_.clear();

  float query_pt[3] = {
    camCtx.cameraPos.x,
    camCtx.cameraPos.y,
    camCtx.cameraPos.z
  };

  const float radius2 = settings.queryRadius * settings.queryRadius;

  struct Hit {
    size_t idx;
    float dist2;
  };
  std::vector<Hit> hits;

  for (size_t i = 0; i < P.particleBlock.particles.size(); ++i) {
    auto& p = P.particleBlock.particles[i];

    if (p.type <= 2)
      continue;

    float dist2 =
      (p.pos[0] - query_pt[0]) * (p.pos[0] - query_pt[0]) +
      (p.pos[1] - query_pt[1]) * (p.pos[1] - query_pt[1]) +
      (p.pos[2] - query_pt[2]) * (p.pos[2] - query_pt[2]);

    if (dist2 > radius2)
      continue;

    hits.push_back({i, dist2});
  }

  std::sort(hits.begin(), hits.end(),
            [](const Hit& a, const Hit& b) { return a.dist2 < b.dist2; });

  for (size_t k = 0; k < hits.size() && k < 50; ++k) {
    const auto& p = P.particleBlock.particles[hits[k].idx];

    ParticleLabelItem item;
    item.worldPos = glm::vec3(p.pos[0], p.pos[1], p.pos[2]);
    item.id = p.ID;

    labels_.push_back(item);
  }

  P.flagParticleIndexDirty = false;
}

void ParticleLabelOverlay::draw(const glm::mat4& view,
                                const glm::mat4& proj,
                                const WindowContext& windowCtx) const
{
  if (labels_.empty())
    return;

  ImGuiIO& io = ImGui::GetIO();
  float scaleX = io.DisplayFramebufferScale.x;
  float scaleY = io.DisplayFramebufferScale.y;
  float FBH    = io.DisplaySize.y * scaleY;

  ImDrawList* draw = ImGui::GetBackgroundDrawList();

  for (const auto& item : labels_) {
    glm::vec4 clip = proj * view * glm::vec4(item.worldPos, 1.0f);

    if (clip.w <= 0.f)
      continue;

    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (std::abs(ndc.x) > 1.f || std::abs(ndc.y) > 1.f)
      continue;

    float px = windowCtx.viewportX() +
               (ndc.x * 0.5f + 0.5f) * float(windowCtx.viewportWidth());
    float py = windowCtx.viewportY() +
               (ndc.y * 0.5f + 0.5f) * float(windowCtx.viewportHeight());

    float sx = px / scaleX;
    float sy = (FBH - py) / scaleY;

    char buf[12];
    std::snprintf(buf, sizeof(buf), "%d", item.id);

    draw->AddRectFilled(ImVec2(sx - 2, sy - 2),
                        ImVec2(sx + ImGui::CalcTextSize(buf).x + 2,
                               sy + ImGui::GetFontSize() + 2),
                        IM_COL32(0, 0, 0, 128), 2.0f);
    draw->AddText(ImVec2(sx, sy), IM_COL32_WHITE, buf);
  }
}
