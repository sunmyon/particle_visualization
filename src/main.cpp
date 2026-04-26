#include <cstddef>
#include <cstdlib>
#include <memory>
#include <mutex>

#include "window_context.h"
#include "render/render_system.h"
#include "platform/local_present.h"
#ifdef PYTHON_BRIDGE
#include "platform/remote_input_receiver.h"
#include "platform/remote_frame_presenter.h"
#endif

#include "app/app_state.h"
#include "app/app_callbacks.h"
#include "app/app_lifecycle.h"
#include "app/app_frame.h"

int main()
{
  WindowContext window;
  AppState app;
  RenderSystem render;
  LocalFramePresenter localPresenter(window);
#ifdef PYTHON_BRIDGE
  std::unique_ptr<RemoteFramePresenter> remotePresenter;
  if (const char* endpoint = std::getenv("PARTICLE_VIS_REMOTE_FRAME_ENDPOINT")) {
    if (endpoint[0] != '\0') {
      remotePresenter =
        std::make_unique<RemoteFramePresenter>(window, std::string(endpoint));
    }
  }
  RemoteInputReceiver remoteInput;
#endif
  CallbackContext callbackCtx;

  if (!InitPlatform(window, callbackCtx, app)) {
    return EXIT_FAILURE;
  }

  InitApplication(app, render);
  LoadInitialData(app);

#ifdef PYTHON_BRIDGE
  if (const char* endpoint = std::getenv("PARTICLE_VIS_REMOTE_INPUT_ENDPOINT")) {
    if (endpoint[0] != '\0') {
      remoteInput.start(endpoint, app.runtime.inputEvents);
    }
  }
#endif

  while (!window.shouldClose()) {
#ifdef PYTHON_BRIDGE
    IFramePresenter& presenter =
      (remotePresenter && remotePresenter->active())
        ? static_cast<IFramePresenter&>(*remotePresenter)
        : static_cast<IFramePresenter&>(localPresenter);
#else
    IFramePresenter& presenter = localPresenter;
#endif
    RunFrame(app, render, window, presenter);
  }

#ifdef PYTHON_BRIDGE
  remoteInput.stop();
#endif
  Cleanup(app, render, window);
  return 0;
}
