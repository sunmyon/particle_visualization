#include "platform/viewport_context.h"

#include <imgui.h>

void ViewportContext::setInitialSize(int width, int height)
{
  initialWidth_ = width;
  initialHeight_ = height;
}

void ViewportContext::updateFramebufferSize(int width, int height)
{
  framebufferWidth_ = width;
  framebufferHeight_ = height;

#ifdef USE_LETTERBOX
  const float targetAspect =
    static_cast<float>(initialWidth_) / static_cast<float>(initialHeight_);
  const float windowAspect =
    static_cast<float>(width) / static_cast<float>(height);

  if (windowAspect > targetAspect) {
    viewportHeight_ = height;
    viewportWidth_  = static_cast<int>(height * targetAspect);
    viewportX_      = (width - viewportWidth_) / 2;
    viewportY_      = 0;
  } else {
    viewportWidth_  = width;
    viewportHeight_ = static_cast<int>(width / targetAspect);
    viewportX_      = 0;
    viewportY_      = (height - viewportHeight_) / 2;
  }
#else
  viewportX_ = 0;
  viewportY_ = 0;
  viewportWidth_ = width;
  viewportHeight_ = height;
#endif
}

glm::vec2 ViewportContext::framebufferToImGui(float px, float py) const
{
  ImGuiIO& io = ImGui::GetIO();

  const float scaleX = io.DisplayFramebufferScale.x;
  const float scaleY = io.DisplayFramebufferScale.y;
  const float fbH    = io.DisplaySize.y * scaleY;

  return glm::vec2(px / scaleX,
                   (fbH - py) / scaleY);
}

glm::vec2 ViewportContext::ndcToFramebuffer(const glm::vec3& ndc) const
{
  const float px = viewportX_ +
                   (ndc.x * 0.5f + 0.5f) * static_cast<float>(viewportWidth_);
  const float py = viewportY_ +
                   (ndc.y * 0.5f + 0.5f) * static_cast<float>(viewportHeight_);

  return glm::vec2(px, py);
}

glm::vec2 ViewportContext::ndcToImGui(const glm::vec3& ndc) const
{
  glm::vec2 fb = ndcToFramebuffer(ndc);
  return framebufferToImGui(fb.x, fb.y);
}

float ViewportContext::projectionAspect() const
{
  if (viewportHeight_ <= 0) {
    return 1.0f;
  }

  return static_cast<float>(viewportWidth_) /
         static_cast<float>(viewportHeight_);
}
