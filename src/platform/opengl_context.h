#pragma once

#include "platform/graphics_context.h"

#include <cstdint>

struct NativeWindowHandle;

class OpenGLContext final : public GraphicsContext {
public:
  void configureWindowHints() const override;
  bool initFromWindow(NativeWindowHandle window) override;
  bool initHeadless(int width, int height) override;
  bool resizeHeadless(int width, int height) override;
  void destroy() override;
  void present(NativeWindowHandle window) override;
  RenderedFrame readDefaultFramebuffer(int width, int height) override;
  bool supportsAsyncReadback() const override { return headless_; }
  bool beginDefaultFramebufferReadback(int width, int height) override;
  RenderedFrame pollDefaultFramebufferReadback() override;

  bool isHeadless() const override { return headless_; }

private:
  struct AsyncReadbackSlot {
    unsigned int buffer = 0;
    void* fence = nullptr;
    int width = 0;
    int height = 0;
    uint64_t sequence = 0;
    bool pending = false;
  };

  void releaseAsyncReadbacks();

  bool headless_ = false;
  AsyncReadbackSlot asyncReadbacks_[2];
  uint64_t asyncReadbackSequence_ = 0;

#ifdef PARTICLE_VIS_HAVE_EGL
  void* eglDisplay_ = nullptr;
  void* eglSurface_ = nullptr;
  void* eglContext_ = nullptr;
  void* eglConfig_ = nullptr;
#endif
};
