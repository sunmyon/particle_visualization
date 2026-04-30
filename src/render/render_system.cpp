#include "render/render_system.h"

#include "render/render_backend.h"

void InitRenderSystem(RenderSystem& rs)
{
  if (!rs.backend) {
    rs.backend = CreateRenderBackend(DefaultRenderBackendKind());
  }

  rs.backend->init();
}

void DestroyRenderSystem(RenderSystem& rs)
{
  if (rs.backend) {
    rs.backend->destroy();
    rs.backend.reset();
  }
}
