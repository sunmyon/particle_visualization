#pragma once

#include <vector>
#include <cstddef>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

class ParticleArray;
struct CameraContext;
struct RenderViewport;
struct ParticleLabelRenderState;

struct ParticleLabelItem {
  glm::vec3 worldPos{0.0f};
  int id = -1;
};

class ParticleLabelOverlay {
public:
  bool needsRebuild(const ParticleArray& particles,
                    const CameraContext& camCtx,
                    const ParticleLabelRenderState& state) const;

  void rebuild(const ParticleArray& particles,
               const CameraContext& camCtx,
               const ParticleLabelRenderState& state);

  void draw(const glm::mat4& view,
            const glm::mat4& proj,
            const RenderViewport& viewport) const;

  void clear() { labels_.clear(); }

private:
  std::vector<ParticleLabelItem> labels_;
};
