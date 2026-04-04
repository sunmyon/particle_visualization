#pragma once

#include <vector>
#include <cstddef>

#include <glm/mat4x4.hpp>

class ParticleArray;
struct CameraContext;
struct SettingsRuntimeState;
class WindowContext;

struct ParticleLabelItem {
  glm::vec3 worldPos{0.0f};
  int id = -1;
};

class ParticleLabelOverlay {
public:
  void updateIfNeeded(ParticleArray& P,
                      const CameraContext& camCtx,
                      SettingsRuntimeState& settings);

  void draw(const glm::mat4& view,
            const glm::mat4& proj,
            const WindowContext& windowCtx) const;

  void clear() { labels_.clear(); }
private:
  std::vector<ParticleLabelItem> labels_;
};
