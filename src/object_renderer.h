#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glad/glad.h>
#include "ui_state.h"
#include "object.h"

class PolyhedronManager;

struct PolyhedronGpuCacheEntry {
  GLuint vao = 0;
  GLuint vbo = 0;
  GLsizei vertexCount = 0;
  bool dirty = true;
  std::string tag;
};

class PolyhedronRenderer {
public:
  void requestResetAll() { resetAllRequested_ = true; }

  void markDirty(int id) {
    auto it = cache_.find(id);
    if (it != cache_.end()) {
      it->second.dirty = true;
    }
  }

  void markDirtyGroup(const std::string& tag){
    for (auto& [id, e] : cache_) {
      if (e.tag == tag) {
	e.dirty = true;
      }
    }
  }
  
  void requestResetGroup(const std::string& tag);
  void removeGpuCache(int id);
  void clearGpuCache();

  void drawWireframe(const PolyhedronManager& manager,
                     const glm::mat4& view,
                     const glm::mat4& projection,
                     GLuint lineProgram);

private:
  bool resetAllRequested_ = false;
  std::unordered_map<int, PolyhedronGpuCacheEntry> cache_;
};

extern PolyhedronRenderer gPolyhedronRenderer;

class EllipsoidManager;
class DiskManager;
class LineManager;
class CubeManager;

class SphereMeshGL {
public:
  struct Vtx {
    glm::vec3 pos;
    glm::vec3 nrm;
  };

  void init(int stacks = 64, int slices = 128);
  void destroy();

  GLuint vao() const { return vao_; }
  GLsizei indexCount() const { return indexCount_; }
  bool ready() const { return vao_ != 0; }

private:
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint ibo_ = 0;
  GLsizei indexCount_ = 0;
};

class EllipsoidRenderer {
public:
  void init();
  void destroy();

  void draw(const EllipsoidManager& manager,
            GLuint program,
            const glm::mat4& view,
            const glm::mat4& projection,
            const RenderLayerState& runtime) const;

private:
  SphereMeshGL sphere_;
};

class DiskMeshGL {
public:
  void init(int slices = 64);
  void destroy();

  GLuint vao() const { return vao_; }
  GLsizei indexCount() const { return indexCount_; }
  bool ready() const { return vao_ != 0; }

private:
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint ebo_ = 0;
  GLsizei indexCount_ = 0;
};

class DiskRenderer {
public:
  void init();
  void destroy();

  void draw(const DiskManager& manager,
            GLuint program,
            const glm::mat4& view,
            const glm::mat4& projection,
            const RenderLayerState& runtime) const;

private:
  DiskMeshGL disk_;
};

class LineRenderer {
public:
  void init();
  void destroy();

  void draw(const LineManager& manager,
            GLuint program,
            const glm::mat4& model,
            const glm::mat4& view,
            const glm::mat4& projection,
            const RenderLayerState& runtime) const;

private:
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
};

class CubeRenderer {
public:
  void init();
  void destroy();

  void sync(const CubeManager& manager,
            RenderLayerState& runtime);

  void draw(const CubeManager& manager,
            GLuint program,
            const glm::mat4& view,
            const glm::mat4& projection,
            const RenderLayerState& runtime) const;

private:
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint ebo_ = 0;
  GLuint instanceVBO_ = 0;
  GLuint opacityVBO_ = 0;
  GLsizei indexCount_ = 36;
  GLsizei instanceCount_ = 0;
};

extern EllipsoidRenderer gEllipsoidRenderer;
extern DiskRenderer      gDiskRenderer;
extern LineRenderer      gLineRenderer;
extern CubeRenderer      gCubeRenderer;


class CrossGizmoRenderer {
public:
  void init();
  void destroy();

  void draw(GLuint lineProgram,
            const glm::mat4& view,
            const glm::mat4& projection,
            const glm::vec3& cameraPos,
            const glm::vec3& cameraTarget,
            const glm::vec3& cameraUp,
            float crossSize) const;

private:
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
};

class CoordAxesRenderer {
public:
  void init();
  void destroy();

  void draw(GLuint coordProgram,
            const glm::mat4& view) const;

private:
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
};

extern CrossGizmoRenderer gCrossGizmoRenderer;
extern CoordAxesRenderer  gCoordAxesRenderer;

struct ColorBarLabelLayout {
  float left_pixel   = 0.0f;
  float right_pixel  = 0.0f;
  float top_pixel    = 0.0f;
  float bottom_pixel = 0.0f;
  float offsetX      = 0.0f;
  float offsetY      = 0.0f;
};

struct ColorbarGizmo {
  bool visible = false;
  int  colormapIndex = 0;
  float valueMin = 0.0f;
  float valueMax = 1.0f;
  int   numTicks = 5;

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

  void draw(GLuint colorbarProgram,
            const ColorbarGizmo& gizmo) const;

  GLuint colormapTexture(int index) const
  {
    if (index < 0) return 0;
    if (static_cast<size_t>(index) >= colormapTextures_.size()) return 0;
    return colormapTextures_[index];
  }
  
  int numColormaps() const { return static_cast<int>(colormapTextures_.size()); }
  
private:
  void updateVertices_(const ColorbarGizmo& gizmo) const;

  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint ebo_ = 0;

  std::vector<GLuint> colormapTextures_;
};

class ColorbarLabelRenderer {
public:
  void draw(const ColorbarGizmo& gizmo) const;
};

extern ColorbarRenderer      gColorbarRenderer;
extern ColorbarLabelRenderer gColorbarLabelRenderer;

class CuboidRenderer {
public:
  void draw(const CuboidObject& obj,
            GLuint lineProgram,
            const glm::mat4& view,
            const glm::mat4& projection,
            const RenderLayerState& runtime) const;

  void drawHighlight(const CuboidAnnotationObject& obj,
                     GLuint lineProgram,
                     const glm::mat4& view,
                     const glm::mat4& projection,
                     const RenderLayerState& runtime) const;
};

class ArrowRenderer {
public:
  void draw(const ArrowObject& obj,
            GLuint lineProgram,
            const glm::mat4& view,
            const glm::mat4& projection,
            const RenderLayerState& runtime) const;
};

extern CuboidRenderer gCuboidRenderer;
extern ArrowRenderer  gArrowRenderer;

class IsoContourRenderer {
public:
  void sync(const TrackingVector<float>& verts,
	    const TrackingVector<unsigned>& inds,
	    RenderLayerState& runtime);
  void draw(GLuint program,
            const glm::mat4& model,
            const glm::mat4& view,
            const glm::mat4& projection,
            const RenderLayerState& runtime) const;
  void destroy();

private:
  GLuint vao_ = 0, vbo_ = 0, nbo_ = 0, ebo_ = 0;
  GLsizei indexCount_ = 0;
};

extern IsoContourRenderer gIsoContourRenderer;
