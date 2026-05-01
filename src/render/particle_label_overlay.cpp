#include "render/particle_label_overlay.h"

#include <algorithm>
#include <cstdio>
#include <cmath>

#include <imgui.h>
#include <glm/glm.hpp>

#include "data/particle_array.h"
#include "data/particle_coordinates.h"
#include "interaction/camera.h"
#include "render/render_state.h"
#include "render/render_viewport.h"

namespace {

struct Hit {
  size_t idx;
  float dist2;
};

static std::vector<Hit> CollectHits(const ParticleArray& particles,
                                    const glm::vec3& queryPos,
                                    float queryRadius,
                                    bool sinkOnly)
{
  std::vector<Hit> hits;
  const float radius2 = queryRadius * queryRadius;

  for (size_t i = 0; i < particles.particleBlock.particles.size(); ++i) {
    const auto& p = particles.particleBlock.particles[i];

    if (sinkOnly && p.type <= 2)
      continue;

    const glm::vec3 pos =
      normalizedParticlePosition(p, particles.particleBlock.normalizedScale);
    const float dx = pos.x - queryPos.x;
    const float dy = pos.y - queryPos.y;
    const float dz = pos.z - queryPos.z;
    const float dist2 = dx * dx + dy * dy + dz * dz;

    if (dist2 > radius2)
      continue;

    hits.push_back({i, dist2});
  }

  std::sort(hits.begin(), hits.end(),
            [](const Hit& a, const Hit& b) { return a.dist2 < b.dist2; });

  return hits;
}

static glm::vec3 ParticleLabelQueryCenter(const CameraContext& camCtx)
{
  return camCtx.cameraTarget;
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

  if (labels_.empty())
    return true;

  if (std::abs(lastQueryRadius_ - state.queryRadius) > 0.0f ||
      lastMaxLabels_ != state.maxLabels ||
      lastSinkOnly_ != state.sinkOnly) {
    return true;
  }

  const glm::vec3 queryCenter = ParticleLabelQueryCenter(camCtx);
  constexpr float kQueryCenterEpsilon = 1.0e-3f;
  return glm::distance(queryCenter, lastQueryCenter_) > kQueryCenterEpsilon;
}

void ParticleLabelOverlay::rebuild(const ParticleArray& particles,
                                   const CameraContext& camCtx,
                                   const ParticleLabelRenderState& state)
{
  labels_.clear();

  const glm::vec3 queryCenter = ParticleLabelQueryCenter(camCtx);
  const std::vector<Hit> hits =
    CollectHits(particles,
                queryCenter,
                std::max(state.queryRadius, 0.0f),
                state.sinkOnly);

  const size_t n =
    std::min(hits.size(),
             static_cast<size_t>(std::max(state.maxLabels, 0)));

  labels_.reserve(n);

  for (size_t k = 0; k < n; ++k) {
    const auto& p = particles.particleBlock.particles[hits[k].idx];

    ParticleLabelItem item;
    item.worldPos =
      normalizedParticlePosition(p, particles.particleBlock.normalizedScale);
    item.id = particles.particleBlock.particleIdSigned(hits[k].idx);
    labels_.push_back(item);
  }

  lastQueryCenter_ = queryCenter;
  lastQueryRadius_ = state.queryRadius;
  lastMaxLabels_ = state.maxLabels;
  lastSinkOnly_ = state.sinkOnly;
}

void ParticleLabelOverlay::draw(const glm::mat4& view,
                                const glm::mat4& proj,
                                const RenderViewport& viewport) const
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

    const glm::vec2 screen = NdcToImGui(viewport, ndc);

    char buf[32];
    std::snprintf(buf,
                  sizeof(buf),
                  "%lld",
                  static_cast<long long>(item.id));

    const ImVec2 textSize = ImGui::CalcTextSize(buf);
    draw->AddRectFilled(ImVec2(screen.x - 2, screen.y - 2),
                        ImVec2(screen.x + textSize.x + 2,
                               screen.y + ImGui::GetFontSize() + 2),
                        IM_COL32(0, 0, 0, 128), 2.0f);
    draw->AddText(ImVec2(screen.x, screen.y), IM_COL32_WHITE, buf);
  }
}
