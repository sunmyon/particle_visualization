#pragma once

#include "platform/graphics_context.h"

struct NativeWindowHandle;

class OpenGLContext final : public GraphicsContext {
public:
  void configureWindowHints() const override;
  bool initFromWindow(NativeWindowHandle window) override;
  bool initHeadless(int width, int height) override;
  void destroy() override;
  void present(NativeWindowHandle window) override;
  RenderedFrame readDefaultFramebuffer(int width, int height) override;

  bool isHeadless() const override { return headless_; }

private:
  bool headless_ = false;

#ifdef PARTICLE_VIS_HAVE_EGL
  void* eglDisplay_ = nullptr;
  void* eglSurface_ = nullptr;
  void* eglContext_ = nullptr;
#endif
};
