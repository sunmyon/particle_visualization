#include "platform/window_context.h"

#include <memory>

WindowContext::WindowContext()
  : windowBackend_(CreateDefaultWindowBackend())
{
}

WindowContext::~WindowContext() = default;

bool WindowContext::init(int width,
                         int height,
                         const char* title,
                         const std::function<void()>& beforeCreateWindow)
{
#ifdef PARTICLE_VIS_HEADLESS_ONLY
  (void)title;
  (void)beforeCreateWindow;
  return initHeadless(width, height);
#else
  viewport_.setInitialSize(width, height);
  headless_ = false;
  closeRequested_ = false;

  if (!windowBackend_->initLibrary()) {
    return false;
  }

  if (beforeCreateWindow) {
    beforeCreateWindow();
  }

  if (!windowBackend_->createWindow(initialWidth(), initialHeight(), title)) {
    windowBackend_->destroy();
    return false;
  }

  int fbW = 0;
  int fbH = 0;
  windowBackend_->framebufferSize(fbW, fbH);
  updateFramebufferSize(fbW, fbH);

  return true;
#endif
}

bool WindowContext::initHeadless(int width, int height)
{
  viewport_.setInitialSize(width, height);
  headless_ = true;
  closeRequested_ = false;
  updateFramebufferSize(width, height);
  return true;
}

void WindowContext::destroy()
{
  windowBackend_->destroy();
}

void WindowContext::requestClose()
{
  closeRequested_ = true;
  windowBackend_->requestClose();
}

void WindowContext::pollEvents()
{
  windowBackend_->pollEvents();
}

bool WindowContext::shouldClose() const
{
  return windowBackend_->shouldClose(closeRequested_);
}

double WindowContext::timeSeconds() const
{
  return windowBackend_->timeSeconds();
}

bool WindowContext::hasWindow() const
{
  return nativeWindowHandle().valid();
}

NativeWindowHandle WindowContext::nativeWindowHandle() const
{
  return windowBackend_->nativeHandle();
}

void WindowContext::updateFramebufferSize(int width, int height)
{
  viewport_.updateFramebufferSize(width, height);
}
