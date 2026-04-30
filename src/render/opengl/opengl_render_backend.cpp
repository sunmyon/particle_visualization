#include "render/opengl/opengl_render_backend.h"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

#include "interaction/camera.h"
#include "render/particle_visual_config.h"
#include "render/particle_lod.h"
#include "projection/projection_map_ui_state.h"
#include "render/colormap_defs.h"
#include "render/frame_matrices.h"
#include "render/opengl/render_draw_helpers.h"
#include "render/overlay_state.h"
#include "render/render_resources.h"
#include "render/render_state.h"
#include "render/render_system.h"
#include "render/render_viewport.h"

#ifndef GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
#define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX 0x9048
#endif
#ifndef GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX 0x9049
#endif
#ifndef GL_TEXTURE_FREE_MEMORY_ATI
#define GL_TEXTURE_FREE_MEMORY_ATI 0x87FC
#endif

static ColorBarLabelLayout ComputeColorbarLayout(
  const RenderViewport& viewport,
  const ColorbarLayoutSettings& settings)
{
  ColorBarLabelLayout layout;

  const float width  = static_cast<float>(viewport.width);
  const float height = static_cast<float>(viewport.height);

  layout.left_pixel   = width  - settings.width  - settings.margin;
  layout.right_pixel  = width  - settings.margin;
  layout.bottom_pixel = height - settings.margin;
  layout.top_pixel    = height - settings.height - settings.margin;

  layout.offsetX = static_cast<float>(viewport.x);
  layout.offsetY = static_cast<float>(viewport.y);

  return layout;
}

static ColorbarGizmoState BuildColorbarGizmoState(
  const RenderRuntimeState& render,
  const ParticleVisualConfig& particleVisual,
  const RenderViewport& viewport)
{
  ColorbarGizmoState state;
  state.visible = render.colorbar.show;

  const int ptype = render.colorbar.sourceParticleType;
  const auto& vis = particleVisual.types[ptype];

  state.content.colormapIndex = vis.colormapIndex;
  state.content.valueMin      = vis.colorMin;
  state.content.valueMax      = vis.colorMax;
  state.content.numTicks      = render.colorbar.numTicks;

  state.effectiveWidth  = static_cast<float>(viewport.width);
  state.effectiveHeight = static_cast<float>(viewport.height);
  state.layout = ComputeColorbarLayout(viewport, render.colorbar.layout);

  return state;
}

static CrossGizmoState BuildCrossGizmoState(
  const CameraContext& camera,
  const CrossGizmoRenderState& gizmo)
{
  CrossGizmoState state;
  state.visible      = gizmo.show;
  state.cameraPos    = camera.cameraPos;
  state.cameraTarget = camera.cameraTarget;
  state.cameraUp     = camera.cameraUp;
  state.crossSize    = gizmo.size;
  return state;
}

static CoordAxesGizmoState BuildCoordAxesGizmoState(
  const CoordAxesRenderState& axes)
{
  CoordAxesGizmoState state;
  state.visible = axes.show;
  return state;
}

static bool EqualMatrix(const glm::mat4& a, const glm::mat4& b)
{
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      if (a[c][r] != b[c][r]) return false;
    }
  }
  return true;
}

static bool EqualParticleTypeVisualConfig(const ParticleTypeVisualConfig& a,
                                          const ParticleTypeVisualConfig& b)
{
  return a.selectedQuantity == b.selectedQuantity &&
         a.pointSize == b.pointSize &&
         a.colorMin == b.colorMin &&
         a.colorMax == b.colorMax &&
         a.useLogScale == b.useLogScale &&
         a.hideParticles == b.hideParticles &&
         a.periodicColorBar == b.periodicColorBar &&
         a.colormapIndex == b.colormapIndex;
}

static bool EqualParticleVisualConfig(const ParticleVisualConfig& a,
                                      const ParticleVisualConfig& b)
{
  for (int i = 0; i < kNumParticleTypes; ++i) {
    if (!EqualParticleTypeVisualConfig(a.types[i], b.types[i])) return false;
  }
  return true;
}

static std::string LowerCopy(std::string text)
{
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

static bool LooksLikeSoftwareRenderer(const std::string& vendor,
                                      const std::string& renderer)
{
  const std::string combined = LowerCopy(vendor + " " + renderer);
  return combined.find("llvmpipe") != std::string::npos ||
         combined.find("softpipe") != std::string::npos ||
         combined.find("software rasterizer") != std::string::npos ||
         combined.find("swr") != std::string::npos ||
         combined.find("mesa offscreen") != std::string::npos;
}

static bool HasExtension(const char* extension)
{
  if (!extension) {
    return false;
  }

  GLint count = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &count);
  if (count > 0 && glGetStringi) {
    for (GLint i = 0; i < count; ++i) {
      const auto* ext =
        reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
      if (ext && std::string(ext) == extension) {
        return true;
      }
    }
    return false;
  }

  const auto* extensions =
    reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
  if (!extensions) {
    return false;
  }
  const std::string all = extensions;
  return all.find(extension) != std::string::npos;
}

static bool ShouldUseParticleLod(const RenderRuntimeState& render,
                                 const std::vector<RenderParticle>& proxy,
                                 bool softwareRenderer)
{
  if (proxy.empty()) {
    return false;
  }

  switch (render.scheduling.particleLod.mode) {
    case ParticleLodMode::Off:
      return render.scheduling.autoParticleLodOnSoftwareRenderer &&
             softwareRenderer &&
             render.scheduling.interactionActive;
    case ParticleLodMode::WhileInteracting:
      return render.scheduling.interactionActive;
    case ParticleLodMode::Always:
      return true;
  }

  return false;
}

static void DestroyParticleFrameCache(OpenGLParticleFrameCache& cache)
{
  if (cache.colorTexture != 0) {
    glDeleteTextures(1, &cache.colorTexture);
    cache.colorTexture = 0;
  }
  if (cache.depthRenderbuffer != 0) {
    glDeleteRenderbuffers(1, &cache.depthRenderbuffer);
    cache.depthRenderbuffer = 0;
  }
  if (cache.framebuffer != 0) {
    glDeleteFramebuffers(1, &cache.framebuffer);
    cache.framebuffer = 0;
  }
  if (cache.vao != 0) {
    glDeleteVertexArrays(1, &cache.vao);
    cache.vao = 0;
  }
  cache.width = 0;
  cache.height = 0;
  cache.particlesVersion = 0;
  cache.valid = false;
}

static bool EnsureParticleFrameCacheTarget(OpenGLParticleFrameCache& cache,
                                           int width,
                                           int height)
{
  width = std::max(width, 1);
  height = std::max(height, 1);

  if (cache.framebuffer == 0) {
    glGenFramebuffers(1, &cache.framebuffer);
  }
  if (cache.colorTexture == 0) {
    glGenTextures(1, &cache.colorTexture);
    glBindTexture(GL_TEXTURE_2D, cache.colorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  if (cache.depthRenderbuffer == 0) {
    glGenRenderbuffers(1, &cache.depthRenderbuffer);
  }
  if (cache.vao == 0) {
    glGenVertexArrays(1, &cache.vao);
  }

  if (cache.width != width || cache.height != height) {
    glBindTexture(GL_TEXTURE_2D, cache.colorTexture);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA16F,
                 width,
                 height,
                 0,
                 GL_RGBA,
                 GL_FLOAT,
                 nullptr);

    glBindRenderbuffer(GL_RENDERBUFFER, cache.depthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER,
                          GL_DEPTH_COMPONENT24,
                          width,
                          height);

    cache.width = width;
    cache.height = height;
    cache.valid = false;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, cache.framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER,
                         GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D,
                         cache.colorTexture,
                         0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                            GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER,
                            cache.depthRenderbuffer);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);

  const bool complete =
    glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  return complete;
}

static bool ParticleFrameCacheMatches(
  const OpenGLParticleFrameCache& cache,
  std::uint64_t particlesVersion,
  const glm::mat4& model,
  const glm::mat4& view,
  const glm::mat4& projection,
  const ParticleVisualConfig& visualConfig,
  int width,
  int height)
{
  return cache.valid &&
         cache.particlesVersion == particlesVersion &&
         cache.width == width &&
         cache.height == height &&
         EqualMatrix(cache.model, model) &&
         EqualMatrix(cache.view, view) &&
         EqualMatrix(cache.projection, projection) &&
         EqualParticleVisualConfig(cache.visualConfig, visualConfig);
}

static void DrawCachedTexture(GLuint texture,
                              GLuint vao,
                              GLuint program)
{
  if (texture == 0 || vao == 0 || program == 0) {
    return;
  }

  glUseProgram(program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  const GLint loc = glGetUniformLocation(program, "uLow");
  if (loc >= 0) glUniform1i(loc, 0);

  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);

  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
}

static void RenderParticlesToCache(OpenGLParticleFrameCache& cache,
                                   const ParticleRenderer& particle,
                                   GLuint particleProgram,
                                   const glm::mat4& model,
                                   const glm::mat4& view,
                                   const glm::mat4& projection,
                                   const ParticleVisualConfig& visualConfig,
                                   const ColorbarRenderer& colorbar,
                                   std::uint64_t particlesVersion,
                                   int width,
                                   int height)
{
  GLint previousFramebuffer = 0;
  GLint previousViewport[4] = {0, 0, 0, 0};
  GLfloat previousClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
  const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
  GLboolean depthWriteWasEnabled = GL_TRUE;
  GLint blendSrcRgb = GL_ONE;
  GLint blendDstRgb = GL_ZERO;
  GLint blendSrcAlpha = GL_ONE;
  GLint blendDstAlpha = GL_ZERO;
  GLint blendEquationRgb = GL_FUNC_ADD;
  GLint blendEquationAlpha = GL_FUNC_ADD;

  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
  glGetIntegerv(GL_VIEWPORT, previousViewport);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteWasEnabled);
  glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationRgb);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha);

  if (!EnsureParticleFrameCacheTarget(cache, width, height)) {
    glBindFramebuffer(GL_FRAMEBUFFER,
                      static_cast<GLuint>(previousFramebuffer));
    return;
  }

  glViewport(0, 0, width, height);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFuncSeparate(GL_SRC_ALPHA,
                      GL_ONE_MINUS_SRC_ALPHA,
                      GL_ONE,
                      GL_ONE_MINUS_SRC_ALPHA);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  particle.draw(particleProgram,
                model,
                view,
                projection,
                visualConfig,
                colorbar);

  cache.particlesVersion = particlesVersion;
  cache.model = model;
  cache.view = view;
  cache.projection = projection;
  cache.visualConfig = visualConfig;
  cache.valid = true;

  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
  glViewport(previousViewport[0],
             previousViewport[1],
             previousViewport[2],
             previousViewport[3]);
  glClearColor(previousClearColor[0],
               previousClearColor[1],
               previousClearColor[2],
               previousClearColor[3]);
  glDepthMask(depthWriteWasEnabled);
  glBlendEquationSeparate(static_cast<GLenum>(blendEquationRgb),
                          static_cast<GLenum>(blendEquationAlpha));
  glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb),
                      static_cast<GLenum>(blendDstRgb),
                      static_cast<GLenum>(blendSrcAlpha),
                      static_cast<GLenum>(blendDstAlpha));
  if (depthTestWasEnabled) {
    glEnable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_DEPTH_TEST);
  }
  if (blendWasEnabled) {
    glEnable(GL_BLEND);
  } else {
    glDisable(GL_BLEND);
  }
}

static void CopyParticleCacheDepthToCurrentFramebuffer(
  const OpenGLParticleFrameCache& cache,
  const RenderViewport& viewport)
{
  if (!cache.valid || cache.framebuffer == 0) {
    return;
  }

  GLint drawFramebuffer = 0;
  GLint readFramebuffer = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, cache.framebuffer);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer));
  glBlitFramebuffer(0,
                    0,
                    cache.width,
                    cache.height,
                    viewport.x,
                    viewport.y,
                    viewport.x + viewport.width,
                    viewport.y + viewport.height,
                    GL_DEPTH_BUFFER_BIT,
                    GL_NEAREST);

  glBindFramebuffer(GL_READ_FRAMEBUFFER,
                    static_cast<GLuint>(readFramebuffer));
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                    static_cast<GLuint>(drawFramebuffer));
}

static void DrawParticleCacheToCurrentFramebuffer(
  const OpenGLParticleFrameCache& cache,
  GLuint program)
{
  if (!cache.valid) {
    return;
  }

  const GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
  const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
  GLboolean depthWriteWasEnabled = GL_TRUE;
  GLint blendSrcRgb = GL_ONE;
  GLint blendDstRgb = GL_ZERO;
  GLint blendSrcAlpha = GL_ONE;
  GLint blendDstAlpha = GL_ZERO;
  GLint blendEquationRgb = GL_FUNC_ADD;
  GLint blendEquationAlpha = GL_FUNC_ADD;

  glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteWasEnabled);
  glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationRgb);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha);

  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  DrawCachedTexture(cache.colorTexture, cache.vao, program);

  glDepthMask(depthWriteWasEnabled);
  glBlendEquationSeparate(static_cast<GLenum>(blendEquationRgb),
                          static_cast<GLenum>(blendEquationAlpha));
  glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb),
                      static_cast<GLenum>(blendDstRgb),
                      static_cast<GLenum>(blendSrcAlpha),
                      static_cast<GLenum>(blendDstAlpha));
  if (depthTestWasEnabled) {
    glEnable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_DEPTH_TEST);
  }
  if (blendWasEnabled) {
    glEnable(GL_BLEND);
  } else {
    glDisable(GL_BLEND);
  }
}

static void DrawStressParticleOverlay(const ParticleRenderer& renderer,
                                      GLuint particleProgram,
                                      const glm::mat4& model,
                                      const glm::mat4& view,
                                      const glm::mat4& projection,
                                      const ParticleVisualConfig& visualConfig,
                                      const ColorbarRenderer& colorbar)
{
  if (renderer.filteredCount() == 0) {
    return;
  }

  const GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
  GLboolean depthWriteWasEnabled = GL_TRUE;
  GLint previousDepthFunc = GL_LESS;
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteWasEnabled);
  glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);

  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDepthFunc(GL_LEQUAL);

  ParticleDrawStyle style;
  style.fixedColor = true;
  style.color = glm::vec4(1.0f, 0.9f, 0.2f, 0.92f);
  style.pointScale = 1.35f;
  renderer.draw(particleProgram,
                model,
                view,
                projection,
                visualConfig,
                colorbar,
                style);

  glDepthFunc(static_cast<GLenum>(previousDepthFunc));
  glDepthMask(depthWriteWasEnabled);
  if (depthTestWasEnabled) {
    glEnable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_DEPTH_TEST);
  }
}

#ifdef VOLUME_RENDERING
static bool EqualVolumeDrawParams(const AdaptiveVolumeDrawParams& a,
                                  const AdaptiveVolumeDrawParams& b)
{
  return EqualMatrix(a.invProjection, b.invProjection) &&
         EqualMatrix(a.invView, b.invView) &&
         a.cameraForward == b.cameraForward &&
         a.resolution == b.resolution &&
         a.focalPixels == b.focalPixels &&
         a.pixelThreshold == b.pixelThreshold &&
         a.tauMax == b.tauMax &&
         a.stepBias == b.stepBias &&
         a.skipEpsilon == b.skipEpsilon &&
         a.debugMode == b.debugMode &&
         a.baseColor == b.baseColor &&
         a.colorMode == b.colorMode &&
         a.tfValueMin == b.tfValueMin &&
         a.tfValueMax == b.tfValueMax &&
         a.tfSigmaScale == b.tfSigmaScale &&
         a.tfMaxSigma == b.tfMaxSigma &&
         a.tfLogScale == b.tfLogScale &&
         a.tfComponentCount == b.tfComponentCount &&
         a.tfTypes == b.tfTypes &&
         a.tfLogDomains == b.tfLogDomains &&
         a.tfCenters == b.tfCenters &&
         a.tfWidths == b.tfWidths &&
         a.tfAmplitudes == b.tfAmplitudes;
}

static void DestroyVolumeFrameCache(
  OpenGLVolumeFrameCache& cache)
{
  if (cache.texture != 0) {
    glDeleteTextures(1, &cache.texture);
    cache.texture = 0;
  }
  if (cache.framebuffer != 0) {
    glDeleteFramebuffers(1, &cache.framebuffer);
    cache.framebuffer = 0;
  }
  if (cache.vao != 0) {
    glDeleteVertexArrays(1, &cache.vao);
    cache.vao = 0;
  }
  cache.width = 0;
  cache.height = 0;
  cache.volumeVersion = 0;
  cache.valid = false;
}

static bool EnsureVolumeFrameCacheTarget(
  OpenGLVolumeFrameCache& cache,
  int width,
  int height)
{
  width = std::max(width, 1);
  height = std::max(height, 1);

  if (cache.framebuffer == 0) {
    glGenFramebuffers(1, &cache.framebuffer);
  }
  if (cache.texture == 0) {
    glGenTextures(1, &cache.texture);
    glBindTexture(GL_TEXTURE_2D, cache.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  if (cache.vao == 0) {
    glGenVertexArrays(1, &cache.vao);
  }

  glBindTexture(GL_TEXTURE_2D, cache.texture);
  if (cache.width != width || cache.height != height) {
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA16F,
                 width,
                 height,
                 0,
                 GL_RGBA,
                 GL_FLOAT,
                 nullptr);
    cache.width = width;
    cache.height = height;
    cache.valid = false;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, cache.framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER,
                         GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D,
                         cache.texture,
                         0);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);

  const bool complete =
    glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindTexture(GL_TEXTURE_2D, 0);
  return complete;
}

static bool VolumeFrameCacheMatches(
  const OpenGLVolumeFrameCache& cache,
  std::uint64_t volumeVersion,
  const AdaptiveVolumeDrawParams& params)
{
  return cache.valid &&
         cache.volumeVersion == volumeVersion &&
         cache.width == params.resolution.x &&
         cache.height == params.resolution.y &&
         EqualVolumeDrawParams(cache.params, params);
}

static void RenderVolumeFrameToCache(
  OpenGLVolumeFrameCache& cache,
  const AdaptiveVolumeRenderer& volume,
  GLuint volumeProgram,
  std::uint64_t volumeVersion,
  const AdaptiveVolumeDrawParams& params)
{
  GLint previousFramebuffer = 0;
  GLint previousViewport[4] = {0, 0, 0, 0};
  GLfloat previousClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
  const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
  GLboolean depthWriteWasEnabled = GL_TRUE;

  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
  glGetIntegerv(GL_VIEWPORT, previousViewport);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteWasEnabled);

  if (!EnsureVolumeFrameCacheTarget(cache,
                                    params.resolution.x,
                                    params.resolution.y)) {
    glBindFramebuffer(GL_FRAMEBUFFER,
                      static_cast<GLuint>(previousFramebuffer));
    return;
  }

  glViewport(0, 0, params.resolution.x, params.resolution.y);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_BLEND);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  volume.draw(volumeProgram, params);

  cache.volumeVersion = volumeVersion;
  cache.params = params;
  cache.valid = true;

  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
  glViewport(previousViewport[0],
             previousViewport[1],
             previousViewport[2],
             previousViewport[3]);
  glClearColor(previousClearColor[0],
               previousClearColor[1],
               previousClearColor[2],
               previousClearColor[3]);
  glDepthMask(depthWriteWasEnabled);
  if (depthTestWasEnabled) {
    glEnable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_DEPTH_TEST);
  }
  if (blendWasEnabled) {
    glEnable(GL_BLEND);
  } else {
    glDisable(GL_BLEND);
  }
}

static void DrawCachedVolumeFrame(
  const OpenGLVolumeFrameCache& cache,
  GLuint program)
{
  if (!cache.valid || cache.texture == 0 || cache.vao == 0 || program == 0) {
    return;
  }

  glUseProgram(program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, cache.texture);
  const GLint loc = glGetUniformLocation(program, "uLow");
  if (loc >= 0) glUniform1i(loc, 0);

  glBindVertexArray(cache.vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);

  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
}
#endif

void OpenGLRenderBackend::init()
{
  InitRenderPrograms(programs_);

  const auto* vendor = glGetString(GL_VENDOR);
  const auto* renderer = glGetString(GL_RENDERER);
  glVendor_ = vendor ? reinterpret_cast<const char*>(vendor) : "(unknown)";
  glRenderer_ = renderer ? reinterpret_cast<const char*>(renderer) : "(unknown)";
  softwareRenderer_ = LooksLikeSoftwareRenderer(glVendor_, glRenderer_);
  hasNvxGpuMemoryInfo_ = HasExtension("GL_NVX_gpu_memory_info");
  hasAtiMeminfo_ = HasExtension("GL_ATI_meminfo");
  std::cout << "OpenGL vendor: " << glVendor_ << "\n"
            << "OpenGL renderer: " << glRenderer_ << "\n"
            << "OpenGL software renderer: "
            << (softwareRenderer_ ? "yes" : "no") << "\n"
            << "OpenGL memory info: "
            << (hasNvxGpuMemoryInfo_ ? "GL_NVX_gpu_memory_info"
                : hasAtiMeminfo_ ? "GL_ATI_meminfo" : "unknown")
            << std::endl;

  particle_.init();
  particleStress_.init();
  particleLod_.init();
  particleLodStress_.init();
  velocity_.init();

  ellipsoid_.init();
  disk_.init();
  line_.init();
  cube_.init();
  cuboid_.init();
  polyhedron_.clearGpuCache();

#ifdef ISO_CONTOUR
  // isocontour_.init(); // Needed only when the renderer owns GL objects.
#endif
#ifdef VOLUME_RENDERING
  volume_.init();
#endif

  crossGizmo_.init();
  coordAxes_.init();

  std::vector<ColormapDefView> cmapViews;
  const ColormapDef* colormaps = AvailableColormaps();
  const int colormapCount = AvailableColormapCount();
  cmapViews.reserve(colormapCount);
  for (int i = 0; i < colormapCount; ++i) {
    cmapViews.push_back({colormaps[i].data, colormaps[i].count});
  }

  colorbar_.init();
  colorbar_.initColorMaps(cmapViews.data(),
                          static_cast<int>(cmapViews.size()));
}

RenderBackendMemoryInfo OpenGLRenderBackend::queryMemoryInfo() const
{
  RenderBackendMemoryInfo info;

  if (hasNvxGpuMemoryInfo_) {
    GLint availableKiB = 0;
    glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX,
                  &availableKiB);
    if (availableKiB > 0) {
      info.gpuAvailableKnown = true;
      info.gpuAvailableBytes =
        static_cast<std::size_t>(availableKiB) * 1024u;
    }
    return info;
  }

  if (hasAtiMeminfo_) {
    GLint values[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, values);
    if (values[0] > 0) {
      info.gpuAvailableKnown = true;
      info.gpuAvailableBytes =
        static_cast<std::size_t>(values[0]) * 1024u;
    }
  }

  return info;
}

void OpenGLRenderBackend::destroy()
{
#ifdef VOLUME_RENDERING
  if (volumeTimingFence_) {
    glDeleteSync(volumeTimingFence_);
    volumeTimingFence_ = nullptr;
  }
  volumeTimingWallStartValid_ = false;
#endif
  crossGizmo_.destroy();
  coordAxes_.destroy();
  DestroyParticleFrameCache(particleFrameCache_);
  velocity_.destroy();
  particleLodStress_.destroy();
  particleLod_.destroy();
  particleStress_.destroy();
  particle_.destroy();

  ellipsoid_.destroy();
  disk_.destroy();
  line_.destroy();
  cube_.destroy();
  cuboid_.destroy();
  colorbar_.destroy();
  polyhedron_.clearGpuCache();
  preview_.destroy();
  DestroyRenderPrograms(programs_);

#ifdef ISO_CONTOUR
  isocontour_.destroy();
#endif
#ifdef VOLUME_RENDERING
  volume_.destroy();
  DestroyVolumeFrameCache(volumeFrameCache_);
#endif
}

void OpenGLRenderBackend::updateProjectionPreview(const RgbImage& image)
{
  preview_.update(image);
}

ProjectionPreviewUIState OpenGLRenderBackend::makeProjectionPreviewUIState() const
{
  return preview_.makeUIState();
}

RenderBackendCapabilities OpenGLRenderBackend::capabilities() const
{
  RenderBackendCapabilities caps;
  caps.particles = true;
  caps.particleLod = true;
  caps.velocityField = true;
  caps.instancedObjects = true;
  caps.lines = true;
  caps.polyhedra = true;
  caps.colorbar = true;
  caps.gizmos = true;
  caps.projectionPreview = true;
  #ifdef ISO_CONTOUR
  caps.isoContour = true;
  #endif
  #ifdef VOLUME_RENDERING
  caps.volumeRendering = true;
  caps.volumeFrameCache = true;
  #endif
  caps.particleFrameCache = true;
  caps.gpuMemoryQuery = hasNvxGpuMemoryInfo_ || hasAtiMeminfo_;
  return caps;
}

#ifdef VOLUME_RENDERING
void OpenGLRenderBackend::pollVolumeTimingFence()
{
  if (!volumeTimingFence_) {
    return;
  }
  const GLenum result = glClientWaitSync(volumeTimingFence_, 0, 0);
  if (result == GL_TIMEOUT_EXPIRED) {
    return;
  }
  glDeleteSync(volumeTimingFence_);
  volumeTimingFence_ = nullptr;
  if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) {
    if (volumeTimingWallStartValid_) {
      const auto now = std::chrono::steady_clock::now();
      timing_.volumeWallLatencyKnown = true;
      timing_.volumeWallLatencyMs =
        std::chrono::duration<double, std::milli>(
          now - volumeTimingWallStart_).count();
    }
  }
  volumeTimingWallStartValid_ = false;
}

void OpenGLRenderBackend::markVolumeTimingFence()
{
  if (volumeTimingFence_) {
    glDeleteSync(volumeTimingFence_);
    volumeTimingFence_ = nullptr;
  }
  volumeTimingFence_ = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  volumeTimingWallStart_ = std::chrono::steady_clock::now();
  volumeTimingWallStartValid_ = volumeTimingFence_ != nullptr;
}
#endif

void OpenGLRenderBackend::render(const RenderFrameState& frame,
                                 const RenderSceneData& scene)
{
#ifdef VOLUME_RENDERING
  pollVolumeTimingFence();
#endif
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (!frame.valid) {
    return;
  }

  glViewport(frame.viewport.x,
             frame.viewport.y,
             frame.viewport.width,
             frame.viewport.height);

  const FrameMatrices& fm = frame.matrices;
  const RenderViewport& viewport = frame.viewport;
  const CameraContext& camera = frame.camera;
  const ParticleVisualConfig& particleVisual = frame.particleVisual;
  const RenderRuntimeState& render = frame.runtime;
  const OverlayState& overlay = frame.overlay;

#ifdef VOLUME_RENDERING
  timing_.volumeCacheUsed = render.scheduling.cacheVolumeFrames;
  timing_.volumeCacheUpdated = false;
  timing_.volumeCacheHit = false;
  timing_.volumeCacheScale =
    std::clamp(static_cast<double>(render.scheduling.volumeFrameCacheScale),
               0.25,
               1.0);
  const bool skipVolumeForInteraction =
    render.scheduling.responsiveInteraction &&
    render.scheduling.interactionActive &&
    render.scheduling.skipVolumeWhileInteracting;
  if (render.volume.show && scene.volume.valid() && !skipVolumeForInteraction) {
    SyncIfVersionChanged(volume_,
                         scene.volume,
                         scene.volumeVersion,
                         uploaded_.volume);

    AdaptiveVolumeDrawParams volumeParams;
    volumeParams.invProjection  = fm.invProj;
    volumeParams.invView        = fm.invView;
    volumeParams.cameraForward  = fm.camForward;
    volumeParams.resolution     = glm::ivec2(viewport.width, viewport.height);
    volumeParams.focalPixels    = fm.focalPx;
    volumeParams.pixelThreshold = render.volume.pixelThreshold;
    volumeParams.tauMax         = render.volume.tauMax;
    volumeParams.stepBias       = render.volume.stepBias;
    volumeParams.skipEpsilon    = render.volume.skipEpsilon;
    volumeParams.debugMode      = render.volume.debugMode;
    volumeParams.baseColor      = render.volume.baseColor;
    volumeParams.colorMode      = std::clamp(render.volume.colorMode, 0, 1);
    volumeParams.tfValueMin     = render.volume.tfValueMin;
    volumeParams.tfValueMax     = render.volume.tfValueMax;
    volumeParams.tfSigmaScale   = render.volume.tfSigmaScale;
    volumeParams.tfMaxSigma     = render.volume.tfMaxSigma;
    volumeParams.tfLogScale     = render.volume.tfLogScale;
    volumeParams.tfComponentCount =
      std::min(static_cast<int>(render.volume.tfComponents.size()), 16);
    for (int i = 0; i < volumeParams.tfComponentCount; ++i) {
      const auto& comp = render.volume.tfComponents[static_cast<std::size_t>(i)];
      volumeParams.tfTypes[static_cast<std::size_t>(i)] = comp.type;
      volumeParams.tfLogDomains[static_cast<std::size_t>(i)] =
        comp.logDomain ? 1 : 0;
      volumeParams.tfCenters[static_cast<std::size_t>(i)] = comp.center;
      volumeParams.tfWidths[static_cast<std::size_t>(i)] = comp.width;
      volumeParams.tfAmplitudes[static_cast<std::size_t>(i)] = comp.amplitude;
    }
    auto drawVolumeLayer = [&](auto&& drawFn) {
      const GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
      const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
      GLboolean depthWriteWasEnabled = GL_TRUE;
      GLint blendSrcRgb = GL_ONE;
      GLint blendDstRgb = GL_ZERO;
      GLint blendSrcAlpha = GL_ONE;
      GLint blendDstAlpha = GL_ZERO;
      GLint blendEquationRgb = GL_FUNC_ADD;
      GLint blendEquationAlpha = GL_FUNC_ADD;
      glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteWasEnabled);
      glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
      glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
      glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
      glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
      glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationRgb);
      glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha);

      glDisable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE);
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      drawFn();

      glDepthMask(depthWriteWasEnabled);
      glBlendEquationSeparate(static_cast<GLenum>(blendEquationRgb),
                              static_cast<GLenum>(blendEquationAlpha));
      glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb),
                          static_cast<GLenum>(blendDstRgb),
                          static_cast<GLenum>(blendSrcAlpha),
                          static_cast<GLenum>(blendDstAlpha));
      if (depthTestWasEnabled) {
        glEnable(GL_DEPTH_TEST);
      } else {
        glDisable(GL_DEPTH_TEST);
      }
      if (blendWasEnabled) {
        glEnable(GL_BLEND);
      } else {
        glDisable(GL_BLEND);
      }
    };

    if (render.scheduling.cacheVolumeFrames) {
      const float cacheScale =
        std::clamp(render.scheduling.volumeFrameCacheScale, 0.25f, 1.0f);
      AdaptiveVolumeDrawParams cacheParams = volumeParams;
      cacheParams.resolution = glm::ivec2(
        std::max(1, static_cast<int>(std::ceil(volumeParams.resolution.x *
                                               cacheScale))),
        std::max(1, static_cast<int>(std::ceil(volumeParams.resolution.y *
                                               cacheScale))));
      cacheParams.focalPixels = volumeParams.focalPixels * cacheScale;
      if (!VolumeFrameCacheMatches(volumeFrameCache_,
                                   scene.volumeVersion,
                                   cacheParams)) {
        RenderVolumeFrameToCache(volumeFrameCache_,
                                 volume_,
                                 programs_.octray,
                                 scene.volumeVersion,
                                 cacheParams);
        markVolumeTimingFence();
        timing_.volumeCacheUpdated = true;
      } else {
        timing_.volumeCacheHit = true;
      }
      drawVolumeLayer([&]() {
        DrawCachedVolumeFrame(volumeFrameCache_, programs_.upscale);
      });
    } else {
      volumeFrameCache_.valid = false;
      drawVolumeLayer([&]() {
        volume_.draw(programs_.octray, volumeParams);
      });
      markVolumeTimingFence();
    }
  } else {
    timing_.volumeCacheUsed = false;
  }
#endif

  SyncIfVersionChanged(particle_,
                       scene.particles,
                       scene.particlesVersion,
                       uploaded_.particles);
  SyncIfVersionChanged(particleStress_,
                       scene.stressParticles,
                       scene.stressParticlesVersion,
                       uploaded_.stressParticles);

  const bool useParticleLod = ShouldUseParticleLod(render,
                                                  scene.particleLodProxy,
                                                  softwareRenderer_);
  bool particlesDrawn = false;
  if (useParticleLod) {
    SyncIfVersionChanged(particleLod_,
                         scene.particleLodProxy,
                         scene.particleLodVersion,
                         uploaded_.particleLod);
    SyncIfVersionChanged(particleLodStress_,
                         scene.particleLodStressProxy,
                         scene.particleLodVersion,
                         uploaded_.particleLodStress);
    particleFrameCache_.valid = false;
    particleLod_.draw(programs_.particle,
                      fm.model,
                      fm.view,
                      fm.projection,
                      particleVisual,
                      colorbar_);
    DrawStressParticleOverlay(particleLodStress_,
                              programs_.particle,
                              fm.model,
                              fm.view,
                              fm.projection,
                              particleVisual,
                              colorbar_);
    particlesDrawn = true;
  }

  const bool useParticleFrameCache =
    render.scheduling.cacheParticleFrames &&
    !render.scheduling.interactionActive;
  if (!particlesDrawn && useParticleFrameCache) {
    if (!ParticleFrameCacheMatches(particleFrameCache_,
                                   scene.particlesVersion,
                                   fm.model,
                                   fm.view,
                                   fm.projection,
                                   particleVisual,
                                   viewport.width,
                                   viewport.height)) {
      RenderParticlesToCache(particleFrameCache_,
                             particle_,
                             programs_.particle,
                             fm.model,
                             fm.view,
                             fm.projection,
                             particleVisual,
                             colorbar_,
                             scene.particlesVersion,
                             viewport.width,
                             viewport.height);
    }
    if (particleFrameCache_.valid) {
      DrawParticleCacheToCurrentFramebuffer(particleFrameCache_,
                                            programs_.textureBlit);
      CopyParticleCacheDepthToCurrentFramebuffer(particleFrameCache_,
                                                 viewport);
    } else {
      particle_.draw(programs_.particle,
                     fm.model,
                     fm.view,
                     fm.projection,
                     particleVisual,
                     colorbar_);
    }
    particlesDrawn = true;
  }

  if (!particlesDrawn) {
    particleFrameCache_.valid = false;
    particle_.draw(programs_.particle,
                   fm.model,
                   fm.view,
                   fm.projection,
                   particleVisual,
                   colorbar_);
  }

  if (!useParticleLod) {
    DrawStressParticleOverlay(particleStress_,
                              programs_.particle,
                              fm.model,
                              fm.view,
                              fm.projection,
                              particleVisual,
                              colorbar_);
  }

  if (render.velocity.show) {
    SyncIfVersionChanged(velocity_,
                         scene.velocityInstances,
                         scene.velocityVersion,
                         uploaded_.velocity);

    velocity_.draw(programs_.velocityArrow,
                   fm.view,
                   fm.projection,
                   render.velocity.arrowScale,
                   render.velocity.useLogScale);
  }

  RenderDrawContext ctx;
  ctx.model            = fm.model;
  ctx.view             = fm.view;
  ctx.projection       = fm.projection;
  ctx.solidProgram     = programs_.instancedSolid;
  ctx.wireProgram      = programs_.line;
  ctx.lineProgram      = programs_.line;
  ctx.coordProgram     = programs_.coord;
  ctx.colorbarProgram  = programs_.colorbar;
#ifdef ISO_CONTOUR
  ctx.isoContourProgram = programs_.isocontour;
#endif

  SyncAndDraw(ellipsoid_,
              scene.ellipsoids,
              scene.ellipsoidsVersion,
              uploaded_.ellipsoids,
              ctx,
              render.ellipsoids);

  SyncAndDraw(disk_,
              scene.disks,
              scene.disksVersion,
              uploaded_.disks,
              ctx,
              render.disks);

  SyncAndDraw(cube_,
              scene.cubes,
              scene.cubesVersion,
              uploaded_.cubes,
              ctx,
              render.cubes);

  SyncAndDraw(cuboid_,
              scene.cuboids,
              scene.cuboidsVersion,
              uploaded_.cuboids,
              ctx,
              render.cuboids);

  SyncAndDraw(line_,
              scene.lines,
              scene.linesVersion,
              uploaded_.lines,
              ctx,
              render.lines);

#ifdef USE_CONVEX_HULL
  SyncAndDraw(polyhedron_,
              scene.polyhedra,
              scene.polyhedraVersion,
              uploaded_.polyhedra,
              ctx,
              render.polyhedra);
#endif

#ifdef ISO_CONTOUR
  SyncAndDraw(isocontour_,
              scene.isoContour,
              scene.isoContourVersion,
              uploaded_.isoContour,
              ctx,
              render.isocontour);
#endif

  overlay.particleLabels.draw(fm.view, fm.projection, viewport);

  GizmoDrawContext gctx;
  gctx.view            = fm.view;
  gctx.projection      = fm.projection;
  gctx.lineProgram     = programs_.line;
  gctx.coordProgram    = programs_.coord;
  gctx.colorbarProgram = programs_.colorbar;

  const CrossGizmoState crossState =
    BuildCrossGizmoState(camera, render.crossGizmo);
  const CoordAxesGizmoState axesState =
    BuildCoordAxesGizmoState(render.coordAxes);
  const ColorbarGizmoState colorbarState =
    BuildColorbarGizmoState(render, particleVisual, viewport);

  crossGizmo_.draw(gctx, crossState);
  coordAxes_.draw(gctx, axesState);
  colorbar_.draw(gctx, colorbarState);
  colorbarLabels_.draw(colorbarState);
}

std::unique_ptr<RenderBackend> CreateOpenGLRenderBackend()
{
  return std::make_unique<OpenGLRenderBackend>();
}
