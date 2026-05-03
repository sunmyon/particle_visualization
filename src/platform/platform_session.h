#pragma once

#include <memory>

#include "platform/graphics_context.h"
#include "platform/local_present.h"
#include "platform/remote_frame_presenter.h"
#include "platform/remote_input_receiver.h"
#include "platform/window_context.h"

struct AppState;
struct CallbackContext;
class RenderBackend;
class VulkanContext;

class PlatformSession {
public:
  PlatformSession();
  ~PlatformSession();

  bool init(AppState& app, CallbackContext& callbackCtx);
  void startRemoteInput(AppState& app);
  void shutdown();

  WindowContext& window() { return window_; }
  const WindowContext& window() const { return window_; }

  IFramePresenter& presenter();
  std::unique_ptr<RenderBackend> createRenderBackend();
  VulkanContext* vulkanContext();

private:
  WindowContext window_;
  std::unique_ptr<GraphicsContext> graphics_;
  std::unique_ptr<LocalFramePresenter> localPresenter_;
  std::unique_ptr<RemoteFramePresenter> remotePresenter_;
  RemoteInputReceiver remoteInput_;
  bool shutdownCalled_ = false;
  bool imguiInitialized_ = false;
  bool nfdInitialized_ = false;
};
