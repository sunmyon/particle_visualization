#include "render_resources.h"

#include <vector>

#include "main.h"
#include "particle_renderer.h"
#include "object_renderer.h"
#include "field_renderer.h"
#include "colormap_defs.h"

bool InitRenderResources(RenderResources& resources, ParticleArray& particles)
{
  gParticleRenderer.init(particles);

  std::vector<ColormapDefView> cmapViews;
  cmapViews.reserve(gNumColormaps);
  for (int i = 0; i < gNumColormaps; ++i) {
    cmapViews.push_back({gColormapDefs[i].data, gColormapDefs[i].count});
  }

  gColorbarRenderer.init();
  gColorbarRenderer.initColorMaps(cmapViews.data(),
                                  static_cast<int>(cmapViews.size()));

  gEllipsoidRenderer.init();
  gDiskRenderer.init();
  gLineRenderer.init();
  gCubeRenderer.init();

  gCrossGizmoRenderer.init();
  gCoordAxesRenderer.init();
  gVelocityFieldRenderer.init();

#ifdef VOLUME_RENDERING
  glGenVertexArrays(1, &resources.fullscreenVAO);
#endif

  return true;
}

void DestroyRenderResources(RenderResources& resources)
{
  gCrossGizmoRenderer.destroy();
  gCoordAxesRenderer.destroy();
  gVelocityFieldRenderer.destroy();
  gParticleRenderer.destroy();

#ifdef ISO_CONTOUR
  gIsoContourRenderer.destroy();
#endif

#ifdef VOLUME_RENDERING
  if (resources.fullscreenVAO != 0) {
    glDeleteVertexArrays(1, &resources.fullscreenVAO);
    resources.fullscreenVAO = 0;
  }
#endif
}
