#pragma once

#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct CameraContext;
struct RenderViewport;

struct ScaleGuideLabelItem {
  glm::vec3 worldPos{0.0f};
  glm::vec3 anchorWorldPos{0.0f};
  float minPixelDistance = 24.0f;
  bool usePixelDistanceCull = false;
  std::string text;
};

class ScaleGuideLabelOverlay {
public:
  void set(std::vector<ScaleGuideLabelItem> labels) {
    labels_ = std::move(labels);
  }

  void clear() { labels_.clear(); }

  void draw(const CameraContext& camera,
            const glm::mat4& view,
            const glm::mat4& proj,
            const RenderViewport& viewport) const;

private:
  std::vector<ScaleGuideLabelItem> labels_;
};
