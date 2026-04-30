#include "render/opengl/gizmo_renderer.h"

#include <cmath>
#include <cstdio>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

void CrossGizmoRenderer::init()
{
  if (vao_ != 0) return;

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               2 * 3 * 3 * sizeof(float),   // 3 lines * 2 endpoints * xyz
               nullptr,
               GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

void CrossGizmoRenderer::destroy()
{
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  vbo_ = 0;
  vao_ = 0;
}

void CrossGizmoRenderer::draw(const GizmoDrawContext& ctx,
                              const CrossGizmoState& state) const
{
  if (!state.visible) return;
  if (vao_ == 0) return;

  GLboolean wasDepthTest = glIsEnabled(GL_DEPTH_TEST);
  glDisable(GL_DEPTH_TEST);
  glLineWidth(3.0f);

  glm::vec3 forward  = glm::normalize(state.cameraTarget - state.cameraPos);
  glm::vec3 rightVec = glm::normalize(glm::cross(forward, state.cameraUp));
  glm::vec3 upVec    = glm::normalize(glm::cross(rightVec, state.cameraUp));
  glm::vec3 upVecNew = glm::normalize(glm::cross(rightVec, forward));

  glm::vec3 v1 = state.cameraTarget - (rightVec + upVec) * state.crossSize;
  glm::vec3 v2 = state.cameraTarget + (rightVec + upVec) * state.crossSize;
  glm::vec3 v3 = state.cameraTarget - (rightVec - upVec) * state.crossSize;
  glm::vec3 v4 = state.cameraTarget + (rightVec - upVec) * state.crossSize;
  glm::vec3 v5 = state.cameraTarget - upVecNew * state.crossSize;
  glm::vec3 v6 = state.cameraTarget + upVecNew * state.crossSize;

  float crossVertices[18] = {
    v1.x, v1.y, v1.z,
    v2.x, v2.y, v2.z,
    v3.x, v3.y, v3.z,
    v4.x, v4.y, v4.z,
    v5.x, v5.y, v5.z,
    v6.x, v6.y, v6.z
  };

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(crossVertices), crossVertices);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glUseProgram(ctx.lineProgram);
  glUniformMatrix4fv(glGetUniformLocation(ctx.lineProgram, "view"),
                     1, GL_FALSE, glm::value_ptr(ctx.view));
  glUniformMatrix4fv(glGetUniformLocation(ctx.lineProgram, "projection"),
                     1, GL_FALSE, glm::value_ptr(ctx.projection));
  glUniform4f(glGetUniformLocation(ctx.lineProgram, "color"),
              1.0f, 1.0f, 1.0f, 1.0f);

  glBindVertexArray(vao_);
  glDrawArrays(GL_LINES, 0, 6);
  glBindVertexArray(0);

  if (wasDepthTest) glEnable(GL_DEPTH_TEST);
}

void CoordAxesRenderer::init()
{
  if (vao_ != 0) return;

  float axesVertsColored[] = {
    // pos       // color
    0,0,0,       1,0,0,
    1,0,0,       1,0,0,

    0,0,0,       0,1,0,
    0,1,0,       0,1,0,

    0,0,0,       1,1,1,
    0,0,1,       1,1,1
  };

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               sizeof(axesVertsColored),
               axesVertsColored,
               GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                        6 * sizeof(float), (void*)0);

  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                        6 * sizeof(float),
                        (void*)(3 * sizeof(float)));

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

void CoordAxesRenderer::destroy()
{
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  vbo_ = 0;
  vao_ = 0;
}

void CoordAxesRenderer::draw(const GizmoDrawContext& ctx,
                             const CoordAxesGizmoState& state) const
{
  if (!state.visible) return;
  if (vao_ == 0) return;

  GLboolean wasDepthTest = glIsEnabled(GL_DEPTH_TEST);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);

  glLineWidth(3.0f);

  glm::mat4 P_ortho = glm::ortho(-1.0f, +1.0f,
                                 -1.0f, +1.0f,
                                 -1.0f, +1.0f);

  glm::mat4 T = glm::translate(glm::mat4(1.0f),
                               glm::vec3(+0.85f, -0.75f, 0.0f));
  glm::mat4 S = glm::scale(glm::mat4(1.0f),
                           glm::vec3(0.1f, 0.1f, 0.1f));

  glm::mat4 R_c = glm::mat4(glm::mat3(ctx.view));
  glm::mat4 model_axes = T * R_c * S;

  glUseProgram(ctx.coordProgram);

  GLint locModel      = glGetUniformLocation(ctx.coordProgram, "model");
  GLint locView       = glGetUniformLocation(ctx.coordProgram, "view");
  GLint locProjection = glGetUniformLocation(ctx.coordProgram, "projection");

  glUniformMatrix4fv(locModel,      1, GL_FALSE, glm::value_ptr(model_axes));
  glUniformMatrix4fv(locView,       1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
  glUniformMatrix4fv(locProjection, 1, GL_FALSE, glm::value_ptr(P_ortho));

  glBindVertexArray(vao_);
  glDrawArrays(GL_LINES, 0, 6);
  glBindVertexArray(0);

  glDepthMask(GL_TRUE);
  if (wasDepthTest) glEnable(GL_DEPTH_TEST);
}

void ColorbarRenderer::init()
{
  if (vao_ != 0) return;

  // x, y, u, v
  float barVertices[] = {
    -0.8f, -0.9f, 0.0f, 0.0f,
    -0.2f, -0.9f, 1.0f, 0.0f,
    -0.2f, -0.8f, 1.0f, 1.0f,
    -0.8f, -0.8f, 0.0f, 1.0f
  };

  unsigned int inds[] = {
    0, 1, 2,
    2, 3, 0
  };

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);

  glBindVertexArray(vao_);

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(barVertices), barVertices, GL_DYNAMIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(inds), inds, GL_STATIC_DRAW);

  // aPos
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                        4 * sizeof(float), (void*)0);

  // aTexCoord
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                        4 * sizeof(float),
                        (void*)(2 * sizeof(float)));

  glBindVertexArray(0);
}

namespace {
GLuint CreateColormapTexture2DRow(const float* rgb, int nColors)
{
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glTexImage2D(GL_TEXTURE_2D,
               0,
               GL_RGB32F,
               nColors,
               1,
               0,
               GL_RGB,
               GL_FLOAT,
               rgb);

  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}
}

void ColorbarRenderer::initColorMaps(const ColormapDefView* defs, int numColormaps)
{
  for (GLuint tex : colormapTextures2D_) {
    if (tex) glDeleteTextures(1, &tex);
  }
  colormapTextures2D_.clear();

  if (!defs || numColormaps <= 0) return;

  colormapTextures2D_.reserve(numColormaps);
  for (int i = 0; i < numColormaps; ++i) {
    colormapTextures2D_.push_back(CreateColormapTexture2DRow(defs[i].data, defs[i].count));
  }
}

void ColorbarRenderer::destroy()
{
  for (GLuint tex : colormapTextures2D_) {
    if (tex) glDeleteTextures(1, &tex);
  }
  colormapTextures2D_.clear();

  if (ebo_) glDeleteBuffers(1, &ebo_);
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);

  ebo_ = 0;
  vbo_ = 0;
  vao_ = 0;
}

inline glm::vec2 PixelToNDC(float x, float y,
                            float effectiveWidth,
                            float effectiveHeight)
{
  float x_ndc = (x / effectiveWidth) * 2.0f - 1.0f;
  float y_ndc = 1.0f - (y / effectiveHeight) * 2.0f;
  return glm::vec2(x_ndc, y_ndc);
}

void ColorbarRenderer::updateVertices_(const ColorbarGizmoState& gizmo) const
{
  float left_pixel   = gizmo.layout.left_pixel;
  float right_pixel  = gizmo.layout.right_pixel;
  float top_pixel    = gizmo.layout.top_pixel;
  float bottom_pixel = gizmo.layout.bottom_pixel;

  glm::vec2 ndc_left_bottom  = PixelToNDC(left_pixel , bottom_pixel, gizmo.effectiveWidth, gizmo.effectiveHeight);
  glm::vec2 ndc_right_bottom = PixelToNDC(right_pixel, bottom_pixel, gizmo.effectiveWidth, gizmo.effectiveHeight);
  glm::vec2 ndc_right_top    = PixelToNDC(right_pixel, top_pixel   , gizmo.effectiveWidth, gizmo.effectiveHeight);
  glm::vec2 ndc_left_top     = PixelToNDC(left_pixel , top_pixel   , gizmo.effectiveWidth, gizmo.effectiveHeight);

  float barVertices[] = {
    ndc_left_bottom.x,  ndc_left_bottom.y,  0.0f, 0.0f,
    ndc_right_bottom.x, ndc_right_bottom.y, 1.0f, 0.0f,
    ndc_right_top.x,    ndc_right_top.y,    1.0f, 1.0f,
    ndc_left_top.x,     ndc_left_top.y,     0.0f, 1.0f
  };

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(barVertices), barVertices);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ColorbarRenderer::draw(const GizmoDrawContext& ctx,
                            const ColorbarGizmoState& gizmo) const
{
  if (!gizmo.visible) return;
  if (vao_ == 0) return;
  if (gizmo.content.colormapIndex < 0) return;
  if (static_cast<size_t>(gizmo.content.colormapIndex) >= colormapTextures2D_.size()) return;

  updateVertices_(gizmo);

  GLboolean wasDepthTest = glIsEnabled(GL_DEPTH_TEST);
  glDisable(GL_DEPTH_TEST);

  glUseProgram(ctx.colorbarProgram);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, colormapTextures2D_[gizmo.content.colormapIndex]);
  glUniform1i(glGetUniformLocation(ctx.colorbarProgram, "colormap"), 0);

  glBindVertexArray(vao_);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

  glBindTexture(GL_TEXTURE_2D, 0);

  if (wasDepthTest) glEnable(GL_DEPTH_TEST);
}

void ColorbarLabelRenderer::draw(const ColorbarGizmoState& gizmo) const
{
  if (!gizmo.visible) return;

  ImGuiIO& io = ImGui::GetIO();
  float scaleX = io.DisplayFramebufferScale.x;
  float scaleY = io.DisplayFramebufferScale.y;

  ImDrawList* drawList = ImGui::GetForegroundDrawList();
  if (!drawList) return;

  const auto& layout = gizmo.layout;
  const int numTicks = gizmo.content.numTicks;

  for (int i = 0; i < numTicks; i++) {
    float t = (numTicks > 1) ? float(i) / float(numTicks - 1) : 0.0f;
    float value =
      gizmo.content.valueMin +
      t * (gizmo.content.valueMax - gizmo.content.valueMin);

    float pxPhys =
      layout.left_pixel + t * (layout.right_pixel - layout.left_pixel);
    float pyPhys = layout.bottom_pixel + 5.0f * scaleY;

    float sx = (pxPhys + layout.offsetX) / scaleX;
    float sy = (pyPhys + layout.offsetY) / scaleY;

    float drawX = std::floor(sx + 0.5f);
    float drawY = std::floor(sy + 0.5f);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", value);

    drawList->AddText(ImVec2(drawX, drawY),
                      IM_COL32(255, 255, 255, 255),
                      buf);
  }
}
