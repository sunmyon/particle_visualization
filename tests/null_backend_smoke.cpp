#include "render/render_backend.h"

#include <cstdlib>
#include <iostream>
#include <memory>

#include "image/rgb_image.h"
#include "projection/projection_map_ui_state.h"
#include "render/render_resources.h"
#include "render/render_system.h"

namespace {

bool Expect(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << "null_backend_smoke: " << message << '\n';
    return false;
  }
  return true;
}

} // namespace

int main()
{
  bool ok = true;

  ok &= Expect(ParseRenderBackendKind("null", RenderBackendKind::OpenGL) ==
                 RenderBackendKind::Null,
               "failed to parse null backend");
  ok &= Expect(ParseRenderBackendKind("opengl", RenderBackendKind::Null) ==
                 RenderBackendKind::OpenGL,
               "failed to parse OpenGL backend");

  #if defined(_WIN32)
  _putenv_s("PARTICLE_VIS_RENDER_BACKEND", "null");
  #else
  setenv("PARTICLE_VIS_RENDER_BACKEND", "null", 1);
  #endif
  ok &= Expect(DefaultRenderBackendKind() == RenderBackendKind::Null,
               "environment backend selection did not choose null");

  std::unique_ptr<RenderBackend> backend =
    CreateRenderBackend(RenderBackendKind::Null);
  ok &= Expect(static_cast<bool>(backend), "CreateRenderBackend(null) failed");
  if (!backend) {
    return EXIT_FAILURE;
  }

  backend->init();

  RenderFrameState frame;
  RenderSceneData scene;
  backend->render(frame, scene);

  RgbImage image;
  image.width = 1;
  image.height = 1;
  image.version = 1;
  image.rgb.push_back(255);
  image.rgb.push_back(0);
  image.rgb.push_back(0);
  backend->updateProjectionPreview(image);

  const ProjectionPreviewUIState preview = backend->makeProjectionPreviewUIState();
  ok &= Expect(!preview.valid, "null backend should not expose a preview texture");
  ok &= Expect(!backend->isSoftwareRenderer(),
               "null backend should not report software OpenGL rendering");
  ok &= Expect(!backend->queryMemoryInfo().gpuAvailableKnown,
               "null backend should not report GPU memory");

  backend->destroy();
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
