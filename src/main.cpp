#include <cstddef>
#include <cstdlib>
#include <memory>
#include <mutex>

#include "window_context.h"
#include "render/render_system.h"
#include "platform/local_present.h"
#include "platform/opengl_context.h"
#include "platform/remote_input_receiver.h"
#include "platform/remote_frame_presenter.h"

#include "app/state/app_state.h"
#include "app/app_lifecycle.h"
#include "app/app_frame.h"

int main()
{
  WindowContext window;
  OpenGLContext graphics;
  AppState app;
  RenderSystem render;
  LocalFramePresenter localPresenter(window, graphics);
  std::unique_ptr<RemoteFramePresenter> remotePresenter;
  RemoteInputReceiver remoteInput;
  CallbackContext callbackCtx;

  if (!InitPlatform(window, graphics, callbackCtx, app)) {
    return EXIT_FAILURE;
  }

  if (const char* endpoint = std::getenv("PARTICLE_VIS_REMOTE_FRAME_ENDPOINT")) {
    if (endpoint[0] != '\0') {
      remotePresenter =
        std::make_unique<RemoteFramePresenter>(window,
                                               graphics,
                                               std::string(endpoint));
    }
  }

  InitApplication(app, render);
  LoadInitialData(app);

  if (const char* endpoint = std::getenv("PARTICLE_VIS_REMOTE_INPUT_ENDPOINT")) {
    if (endpoint[0] != '\0') {
      remoteInput.start(endpoint, app.runtime.inputEvents);
    }
  }

  while (!window.shouldClose()) {
    IFramePresenter& presenter =
      (remotePresenter && remotePresenter->active())
        ? static_cast<IFramePresenter&>(*remotePresenter)
        : static_cast<IFramePresenter&>(localPresenter);
    RunFrame(app, render, window, presenter);
  }

  remoteInput.stop();
  Cleanup(app, render, graphics, window);
  return 0;
}
