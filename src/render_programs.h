#pragma once

#include <glad/glad.h>

struct RenderPrograms {
  GLuint particle = 0;
  GLuint velocityArrow = 0;
  GLuint line = 0;
  GLuint ellipsoid = 0;
  GLuint disk = 0;
  GLuint cubic = 0;
  GLuint coord = 0;
  GLuint colorbar = 0;

#ifdef ISO_CONTOUR
  GLuint isocontour = 0;
#endif

#ifdef VOLUME_RENDERING
  GLuint rt = 0;
  GLuint octray = 0;
  GLuint upscale = 0;
  GLuint wboitParticle = 0;
  GLuint wboitComposite = 0;
#endif
};

bool InitRenderPrograms(RenderPrograms& p);
void DestroyRenderPrograms(RenderPrograms& p);
