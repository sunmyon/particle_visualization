#include <cstddef>
#include <cstdlib>
#include <mutex>

#include "window_context.h"
#include "render/render_system.h"

#include "app/app_state.h"
#include "app/app_callbacks.h"
#include "app/app_lifecycle.h"
#include "app/app_frame.h"

int main()
{
  WindowContext window;
  AppState app;
  RenderSystem render;
  CallbackContext callbackCtx;

  if (!InitPlatform(window, callbackCtx, app)) {
    return EXIT_FAILURE;
  }

  InitApplication(app, render);
  LoadInitialData(app);

  while (!glfwWindowShouldClose(window.handle())) {
    RunFrame(app, render, window);
  }

  Cleanup(app, render, window);
  return 0;
}

