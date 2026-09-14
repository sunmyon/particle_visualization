#pragma once

#include <functional>
#include <memory>

#include "platform/window_backend.h"
#include "platform/viewport_context.h"

class WindowContext {
public:
  WindowContext();
  ~WindowContext();

  bool init(int width,
            int height,
            const char* title,
            const std::function<void()>& beforeCreateWindow = {});
  bool initHeadless(int width, int height);
  void destroy();

  void updateFramebufferSize(int width, int height);
  void updateRemoteFramebufferSize(int width,
                                   int height,
                                   int displayWidth,
                                   int displayHeight,
                                   float framebufferScaleX,
                                   float framebufferScaleY);
  void pollEvents();
  void requestClose();
  bool shouldClose() const;
  double timeSeconds() const;

  bool hasWindow() const;
  NativeWindowHandle nativeWindowHandle() const;
  bool isHeadless() const { return headless_; }
  const ViewportContext& viewport() const { return viewport_; }

  int initialWidth() const { return viewport_.initialWidth(); }
  int initialHeight() const { return viewport_.initialHeight(); }

  int viewportX() const { return viewport_.viewportX(); }
  int viewportY() const { return viewport_.viewportY(); }
  int viewportWidth() const { return viewport_.viewportWidth(); }
  int viewportHeight() const { return viewport_.viewportHeight(); }
  int framebufferWidth() const { return viewport_.framebufferWidth(); }
  int framebufferHeight() const { return viewport_.framebufferHeight(); }
  int displayWidth() const { return displayWidth_; }
  int displayHeight() const { return displayHeight_; }
  float framebufferScaleX() const { return framebufferScaleX_; }
  float framebufferScaleY() const { return framebufferScaleY_; }

private:
  bool headless_ = false;
  bool closeRequested_ = false;

  std::unique_ptr<WindowBackend> windowBackend_;
  ViewportContext viewport_;
  int displayWidth_ = 1280;
  int displayHeight_ = 720;
  float framebufferScaleX_ = 1.0f;
  float framebufferScaleY_ = 1.0f;
};
