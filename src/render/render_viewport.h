#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct RenderViewport {
  int x = 0;
  int y = 0;
  int width = 1;
  int height = 1;
  int framebufferWidth = 1;
  int framebufferHeight = 1;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;

  float aspect() const {
    return (height > 0) ? static_cast<float>(width) / static_cast<float>(height)
                        : 1.0f;
  }
};

inline glm::vec2 NdcToFramebuffer(const RenderViewport& viewport,
                                  const glm::vec3& ndc)
{
  const float px = static_cast<float>(viewport.x) +
                   (ndc.x * 0.5f + 0.5f) *
                     static_cast<float>(viewport.width);
  const float py = static_cast<float>(viewport.y) +
                   (ndc.y * 0.5f + 0.5f) *
                     static_cast<float>(viewport.height);
  return glm::vec2(px, py);
}

inline glm::vec2 FramebufferToImGui(const RenderViewport& viewport,
                                    float px,
                                    float py)
{
  return glm::vec2(px / viewport.framebufferScaleX,
                   (static_cast<float>(viewport.framebufferHeight) - py) /
                     viewport.framebufferScaleY);
}

inline glm::vec2 NdcToImGui(const RenderViewport& viewport,
                            const glm::vec3& ndc)
{
  const glm::vec2 fb = NdcToFramebuffer(viewport, ndc);
  return FramebufferToImGui(viewport, fb.x, fb.y);
}
