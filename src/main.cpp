#include <cstdlib>
#include <memory>
#include <string>

#include "render/render_backend.h"
#include "render/render_system.h"
#include "platform/platform_session.h"

#include "app/state/app_state.h"
#include "app/app_lifecycle.h"
#include "app/app_frame.h"
#include "app/volume_gpu_batch.h"
#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
#include "projection/vulkan_projection_backend.h"
#endif

int main(int argc, char** argv)
{
  if (argc == 3 && std::string(argv[1]) == "--volume-gpu-batch") {
    return RunVolumeGpuBatchFromJson(argv[2]);
  }

  PlatformSession platform;
  AppState app;
  RenderSystem render;
  CallbackContext callbackCtx;

  if (!platform.init(app, callbackCtx)) {
    return EXIT_FAILURE;
  }

  if (std::unique_ptr<RenderBackend> platformBackend =
        platform.createRenderBackend()) {
    render.backend = std::move(platformBackend);
  }
  InitApplication(app, render);
#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
  SetVulkanProjectionContext(platform.vulkanContext());
#endif
  LoadInitialData(app);
  platform.startRemoteInput(app);

  while (!platform.window().shouldClose()) {
    RunFrame(app, render, platform.window(), platform.presenter());
  }

#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
  SetVulkanProjectionContext(nullptr);
#endif
  Cleanup(app, render);
  platform.shutdown();
  return 0;
}
