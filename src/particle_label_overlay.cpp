#include "particle_label_overlay.h"

#include <algorithm>
#include <cstdio>
#include <cmath>

#include <imgui.h>
#include <glm/glm.hpp>

#include "data/particle_array.h"
#include "app/render_runtime_state.h"
#include "interaction/camera.h"
#include "window_context.h"

namespace {

struct Hit {
  size_t idx;
  float dist2;
};

static std::vector<Hit> CollectHits(const ParticleArray& particles,
                                    const glm::vec3& queryPos,
                                    float queryRadius)
{
  std::vector<Hit> hits;
  const float radius2 = queryRadius * queryRadius;

  for (size_t i = 0; i < particles.particleBlock.particles.size(); ++i) {
    const auto& p = particles.particleBlock.particles[i];

    if (p.type <= 2)
      continue;

    const float dx = p.pos[0] - queryPos.x;
    const float dy = p.pos[1] - queryPos.y;
    const float dz = p.pos[2] - queryPos.z;
    const float dist2 = dx * dx + dy * dy + dz * dz;

    if (dist2 > radius2)
      continue;

    hits.push_back({i, dist2});
  }

  std::sort(hits.begin(), hits.end(),
            [](const Hit& a, const Hit& b) { return a.dist2 < b.dist2; });

  return hits;
}

} // namespace

bool ParticleLabelOverlay::needsRebuild(const ParticleArray& particles,
                                        const CameraContext& camCtx,
                                        const ParticleLabelRenderState& state) const
{
  if (!state.show)
    return false;

  if (particles.flagParticleIndexDirty)
    return true;

  return glm::distance(camCtx.cameraPos, state.lastCameraPos) >= state.moveThreshold;
}

void ParticleLabelOverlay::rebuild(const ParticleArray& particles,
                                   const CameraContext& camCtx,
                                   ParticleLabelRenderState& state)
{
  state.lastCameraPos = camCtx.cameraPos;
  labels_.clear();

  const std::vector<Hit> hits =
    CollectHits(particles, camCtx.cameraPos, state.queryRadius);

  const size_t n =
    std::min(hits.size(), static_cast<size_t>(state.maxLabels));

  labels_.reserve(n);

  for (size_t k = 0; k < n; ++k) {
    const auto& p = particles.particleBlock.particles[hits[k].idx];

    ParticleLabelItem item;
    item.worldPos = glm::vec3(p.pos[0], p.pos[1], p.pos[2]);
    item.id = p.ID;
    labels_.push_back(item);
  }
}

void ParticleLabelOverlay::draw(const glm::mat4& view,
                                const glm::mat4& proj,
                                const WindowContext& windowCtx) const
{
  if (labels_.empty())
    return;

  ImDrawList* draw = ImGui::GetBackgroundDrawList();

  for (const auto& item : labels_) {
    const glm::vec4 clip = proj * view * glm::vec4(item.worldPos, 1.0f);

    if (clip.w <= 0.f)
      continue;

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (std::abs(ndc.x) > 1.f || std::abs(ndc.y) > 1.f)
      continue;

    const glm::vec2 screen = windowCtx.ndcToImGui(ndc);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", item.id);

    const ImVec2 textSize = ImGui::CalcTextSize(buf);
    draw->AddRectFilled(ImVec2(screen.x - 2, screen.y - 2),
                        ImVec2(screen.x + textSize.x + 2,
                               screen.y + ImGui::GetFontSize() + 2),
                        IM_COL32(0, 0, 0, 128), 2.0f);
    draw->AddText(ImVec2(screen.x, screen.y), IM_COL32_WHITE, buf);
  }
}
