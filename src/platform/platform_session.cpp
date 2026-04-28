#include "platform/platform_session.h"

#include "app/state/app_state.h"
#ifndef PARTICLE_VIS_HEADLESS_ONLY
#include "app/app_callbacks.h"
#endif
#include "platform/imgui_context.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#ifndef NONATIVEFILEDIALOG
#include <nfd.h>
#endif

PlatformSession::PlatformSession()
{
  graphics_ = CreateDefaultGraphicsContext();
  if (graphics_) {
    localPresenter_ =
      std::make_unique<LocalFramePresenter>(window_, *graphics_);
  }
}

PlatformSession::~PlatformSession()
{
  shutdown();
}

bool PlatformSession::init(AppState& app, CallbackContext& callbackCtx)
{
  shutdownCalled_ = false;
  if (!graphics_ || !localPresenter_) {
    return false;
  }

  bool initialized = false;
#ifdef PYTHON_BRIDGE
  const char* remoteFrameEndpoint =
    std::getenv("PARTICLE_VIS_REMOTE_FRAME_ENDPOINT");
  const bool remoteMode =
    remoteFrameEndpoint && remoteFrameEndpoint[0] != '\0';
  const char* headlessEnv = std::getenv("PARTICLE_VIS_HEADLESS");
  const bool allowHeadless = !headlessEnv || std::string(headlessEnv) != "0";
  if (remoteMode && allowHeadless) {
    initialized = window_.initHeadless(1280, 720);
    if (initialized) {
      initialized = graphics_->initHeadless(window_.framebufferWidth(),
                                            window_.framebufferHeight());
    }
  }
#endif

  if (!initialized) {
    initialized =
      window_.init(1280,
                   720,
                   "3D Particle Visualization",
                   [this]() {
                     graphics_->configureWindowHints();
                   });
    if (initialized) {
      if (window_.hasWindow()) {
        initialized = graphics_->initFromWindow(window_.nativeWindowHandle());
      } else {
        initialized = graphics_->initHeadless(window_.framebufferWidth(),
                                              window_.framebufferHeight());
      }
    }
  }

  if (!initialized) {
    shutdown();
    return false;
  }

  callbackCtx.app = &app;
  callbackCtx.window = &window_;

  std::unique_ptr<ImGuiBackend> imguiBackend;
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (window_.hasWindow()) {
    AttachAppCallbacks(window_, callbackCtx);
    imguiBackend =
      CreateGlfwOpenGLImGuiBackend(window_.nativeWindowHandle());
  } else {
#endif
    imguiBackend =
      CreateHeadlessOpenGLImGuiBackend(window_.framebufferWidth(),
                                       window_.framebufferHeight());
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  }
#endif
  if (!imguiBackend) {
    shutdown();
    return false;
  }
  InitImGuiContext(std::move(imguiBackend));
  imguiInitialized_ = true;

#ifndef NONATIVEFILEDIALOG
  if (NFD_Init() != NFD_OKAY) {
    std::cerr << "NFD_Init failed: " << NFD_GetError() << std::endl;
  } else {
    nfdInitialized_ = true;
  }
#endif

  if (const char* endpoint = std::getenv("PARTICLE_VIS_REMOTE_FRAME_ENDPOINT")) {
    if (endpoint[0] != '\0') {
      remotePresenter_ =
        std::make_unique<RemoteFramePresenter>(window_,
                                               *graphics_,
                                               std::string(endpoint));
    }
  }

  return true;
}

void PlatformSession::startRemoteInput(AppState& app)
{
  if (const char* endpoint = std::getenv("PARTICLE_VIS_REMOTE_INPUT_ENDPOINT")) {
    if (endpoint[0] != '\0') {
      remoteInput_.start(endpoint, app.runtime.inputEvents);
    }
  }
}

IFramePresenter& PlatformSession::presenter()
{
  if (remotePresenter_ && remotePresenter_->active()) {
    return *remotePresenter_;
  }
  return *localPresenter_;
}

void PlatformSession::shutdown()
{
  if (shutdownCalled_) {
    return;
  }
  shutdownCalled_ = true;

  remoteInput_.stop();

#ifndef NONATIVEFILEDIALOG
  if (nfdInitialized_) {
    NFD_Quit();
    nfdInitialized_ = false;
  }
#endif

  if (imguiInitialized_) {
    ShutdownImGuiContext();
    imguiInitialized_ = false;
  }
  if (graphics_) {
    graphics_->destroy();
  }
  window_.destroy();
}
