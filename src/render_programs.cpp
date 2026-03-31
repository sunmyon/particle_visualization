#include "render_programs.h"

#include "shader_utils.h"
#include "shader_sources.h"

bool InitRenderPrograms(RenderPrograms& p)
{
  p.colorbar = createShaderProgram(colorbarVertexShaderSource,
                                   colorbarFragmentShaderSource);

  p.particle = createShaderProgram(particleVertexShaderSource,
                                   particleFragmentShaderSource);

  p.line = createShaderProgram(lineVertexShaderSource,
                               lineFragmentShaderSource);

  p.velocityArrow = createShaderProgram(velocityArrowVertexShaderSource,
                                        velocityArrowFragmentShaderSource);

  p.cubic = createShaderProgram(cubicShaderSource,
                                cubicFragmentShaderSource);

  p.ellipsoid = createShaderProgram(ellipsoidVertexShaderSource,
                                    ellipsoidFragmentShaderSource);

  p.disk = createShaderProgram(diskVertexShaderSource,
                               diskFragmentShaderSource);

  p.coord = createShaderProgram(coordShaderSource,
                                coordFragmentShaderSource);

#ifdef ISO_CONTOUR
  p.isocontour = createShaderProgram(isocontourVertexShaderSource,
                                     isocontourFragmentShaderSource);
#endif

#ifdef VOLUME_RENDERING
  p.rt = createShaderProgramWithHeader(fullscreenShaderSource,
                                       rtFragmentShaderSource,
                                       shaderHeader410);

  p.octray = createShaderProgramWithHeader(fullscreenShaderSource,
                                           octrayFragmentShaderSource,
                                           shaderHeader410);

  p.upscale = createShaderProgramWithHeader(upscaleVS,
                                            upscaleFS,
                                            shaderHeader410);

  p.wboitParticle = createShaderProgramWithHeader(wboitParticleShaderSource,
                                                  wboitParticleFragmentShaderSource,
                                                  shaderHeader410);

  p.wboitComposite = createShaderProgramWithHeader(wboitResolveShaderSource,
                                                   wboitResolveFragmentShaderSource,
                                                   shaderHeader410);
#endif

  return true;
}

static void DeleteProgram(GLuint& program)
{
  if (program != 0) {
    glDeleteProgram(program);
    program = 0;
  }
}

void DestroyRenderPrograms(RenderPrograms& p)
{
  DeleteProgram(p.particle);
  DeleteProgram(p.velocityArrow);
  DeleteProgram(p.line);
  DeleteProgram(p.ellipsoid);
  DeleteProgram(p.disk);
  DeleteProgram(p.cubic);
  DeleteProgram(p.coord);
  DeleteProgram(p.colorbar);

#ifdef ISO_CONTOUR
  DeleteProgram(p.isocontour);
#endif

#ifdef VOLUME_RENDERING
  DeleteProgram(p.rt);
  DeleteProgram(p.octray);
  DeleteProgram(p.upscale);
  DeleteProgram(p.wboitParticle);
  DeleteProgram(p.wboitComposite);
#endif
}
