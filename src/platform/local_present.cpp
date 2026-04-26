#include "platform/local_present.h"

#include <algorithm>

#include <glad/glad.h>

#include "imgui_context.h"
#include "window_context.h"

namespace {

RenderedFrame ReadBackDefaultFramebuffer(const WindowContext& window)
{
  RenderedFrame frame;
  frame.width = window.framebufferWidth();
  frame.height = window.framebufferHeight();
  if (frame.width <= 0 || frame.height <= 0) {
    return frame;
  }

  frame.format = RenderedFrameFormat::RGBA8;
  frame.pixels.resize(static_cast<size_t>(frame.width) *
                      static_cast<size_t>(frame.height) * 4);

  GLint prevPackAlignment = 4;
  glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlignment);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(0,
               0,
               frame.width,
               frame.height,
               GL_RGBA,
               GL_UNSIGNED_BYTE,
               frame.pixels.data());
  glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlignment);

  const size_t stride = static_cast<size_t>(frame.width) * 4;
  std::vector<uint8_t> row(stride);
  for (int y = 0; y < frame.height / 2; ++y) {
    uint8_t* top = frame.pixels.data() + static_cast<size_t>(y) * stride;
    uint8_t* bottom =
      frame.pixels.data() + static_cast<size_t>(frame.height - 1 - y) * stride;
    std::copy(top, top + stride, row.data());
    std::copy(bottom, bottom + stride, top);
    std::copy(row.data(), row.data() + stride, bottom);
  }

  return frame;
}

} // namespace

PresentResult PresentLocalFrame(WindowContext& window,
                                const PresentOptions& options)
{
  EndImGuiFrame();

  PresentResult result;
  if (options.readbackFrame) {
    result.frame = ReadBackDefaultFramebuffer(window);
  }

  window.present();
  result.presented = true;
  return result;
}

LocalFramePresenter::LocalFramePresenter(WindowContext& window)
  : window_(&window)
{
}

PresentResult LocalFramePresenter::present(const PresentOptions& options)
{
  if (!window_) {
    return PresentResult{};
  }
  return PresentLocalFrame(*window_, options);
}
