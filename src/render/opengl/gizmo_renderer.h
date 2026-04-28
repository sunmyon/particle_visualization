#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>

struct GizmoDrawContext {
  glm::mat4 view{1.0f};
  glm::mat4 projection{1.0f};

  GLuint lineProgram = 0;
  GLuint coordProgram = 0;
  GLuint colorbarProgram = 0;
};

struct CrossGizmoState {
  bool visible = true;
  glm::vec3 cameraPos{0.0f};
  glm::vec3 cameraTarget{0.0f};
  glm::vec3 cameraUp{0.0f, 1.0f, 0.0f};
  float crossSize = 0.05f;
};

struct CoordAxesGizmoState {
  bool visible = true;
};

class CrossGizmoRenderer {
public:
  void init();
  void destroy();

  void draw(const GizmoDrawContext& ctx,
            const CrossGizmoState& state) const;

private:
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
};

class CoordAxesRenderer {
public:
  void init();
  void destroy();

  void draw(const GizmoDrawContext& ctx,
            const CoordAxesGizmoState& state) const;

private:
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
};

struct ColorBarLabelLayout {
  float left_pixel   = 0.0f;
  float right_pixel  = 0.0f;
  float top_pixel    = 0.0f;
  float bottom_pixel = 0.0f;
  float offsetX      = 0.0f;
  float offsetY      = 0.0f;
};

struct ColorbarContentState {
  int colormapIndex = 0;
  float valueMin = 0.0f;
  float valueMax = 1.0f;
  int numTicks = 5;
};

struct ColorbarGizmoState {
  bool visible = false;

  ColorbarContentState content;

  float effectiveWidth  = 1.0f;
  float effectiveHeight = 1.0f;

  ColorBarLabelLayout layout;
};

struct ColormapDefView {
  const float* data = nullptr;
  int count = 0;
};

class ColorbarRenderer {
public:
  void init();
  void destroy();

  void initColorMaps(const ColormapDefView* defs, int numColormaps);

  void draw(const GizmoDrawContext& ctx,
            const ColorbarGizmoState& gizmo) const;

  GLuint colormapTexture(int index) const
  {
    if (index < 0) return 0;
    if (static_cast<size_t>(index) >= colormapTextures_.size()) return 0;
    return colormapTextures_[index];
  }

  int numColormaps() const { return static_cast<int>(colormapTextures_.size()); }

private:
  void updateVertices_(const ColorbarGizmoState& gizmo) const;

  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint ebo_ = 0;

  std::vector<GLuint> colormapTextures_;
};


class ColorbarLabelRenderer {
public:
  void draw(const ColorbarGizmoState& gizmo) const;
};

