#include "platform/local_present.h"

#include "platform/graphics_context.h"
#include "platform/imgui_context.h"
#include "platform/window_context.h"

PresentResult PresentLocalFrame(WindowContext& window,
                                GraphicsContext& graphics,
                                const PresentOptions& options)
{
  EndImGuiFrame();

  PresentResult result;
  if (options.readbackFrame) {
    result.frame = graphics.readDefaultFramebuffer(window.framebufferWidth(),
                                                   window.framebufferHeight());
  }

  graphics.present(window.nativeWindowHandle());
  result.presented = true;
  return result;
}

LocalFramePresenter::LocalFramePresenter(WindowContext& window,
                                         GraphicsContext& graphics)
  : window_(&window)
  , graphics_(&graphics)
{
}

PresentResult LocalFramePresenter::present(const PresentOptions& options)
{
  if (!window_ || !graphics_) {
    return PresentResult{};
  }
  return PresentLocalFrame(*window_, *graphics_, options);
}
