#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glad/glad.h>
#include "app/render_runtime_state.h"
#include "object.h"
#include "render_resources.h"
#include "render_types.h"

struct RenderDrawContext {
  glm::mat4 model{1.0f};
  glm::mat4 view{1.0f};
  glm::mat4 projection{1.0f};

  GLuint solidProgram = 0;
  GLuint wireProgram = 0;
  GLuint lineProgram = 0;
  GLuint coordProgram = 0;
  GLuint colorbarProgram = 0;
  GLuint isoContourProgram = 0;
};

struct PolyhedronGpuCacheEntry {
  GLuint vao = 0;
  GLuint vbo = 0;
  GLsizei vertexCount = 0;
  glm::vec3 color{1.0f};
  float opacity = 1.0f;
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

  void sync(const std::vector<PolyhedronRenderItem>& data);
  void draw(const RenderDrawContext& ctx,
	    const RenderLayerState& runtime) const;  
private:
  bool resetAllRequested_ = false;
  std::unordered_map<int, PolyhedronGpuCacheEntry> cache_;
};


enum class SurfaceMode {
  Solid,
  Wire,
  SolidAndWire
};

struct SolidInstanceBuffers {
  GLuint modelVBO   = 0;
  GLuint colorVBO   = 0;
  GLuint opacityVBO = 0;
  GLsizei instanceCount = 0;
};

class IndexedMeshGL {
public:
  void destroy();

  GLuint vao() const { return vao_; }
  GLsizei indexCount() const { return indexCount_; }
  bool ready() const { return vao_ != 0; }

protected:
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint ebo_ = 0;
  GLsizei indexCount_ = 0;
};

class InstancedSolidRendererBase {
public:
  virtual ~InstancedSolidRendererBase() = default;

  void setSurfaceMode(SurfaceMode mode) { mode_ = mode; }
  SurfaceMode surfaceMode() const { return mode_; }

protected:
  void initSolidInstanceAttributes_(GLuint vao, SolidInstanceBuffers& buffers);
  void destroySolidInstanceBuffers_(SolidInstanceBuffers& buffers);
  void syncSolidInstanceBuffers_(const std::vector<InstancedSolidItem>& data,
                                 SolidInstanceBuffers& buffers);

  void drawSolidInstanced_(GLuint vao,
                           GLsizei indexCount,
                           GLuint program,
                           const glm::mat4& view,
                           const glm::mat4& projection,
                           const RenderLayerState& runtime,
                           GLsizei instanceCount) const;

private:
  SurfaceMode mode_ = SurfaceMode::Solid;
};


class SphereMeshGL : public IndexedMeshGL {
public:
  struct Vtx {
    glm::vec3 pos;
    glm::vec3 nrm;
  };

  void init(int stacks = 64, int slices = 128);
};

class DiskMeshGL : public IndexedMeshGL {
public:
  void init(int slices = 64);
};

class CubeMeshGL : public IndexedMeshGL {
public:
  void init();
};

class EllipsoidRenderer : public InstancedSolidRendererBase {
public:
  void init();
  void destroy();

  void sync(const std::vector<InstancedSolidItem>& data);
  void draw(const RenderDrawContext& ctx,
	    const RenderLayerState& runtime) const;  
private:
  SphereMeshGL mesh_;
  SolidInstanceBuffers solid_;
};

class DiskRenderer : public InstancedSolidRendererBase {
public:
  void init();
  void destroy();

  void sync(const std::vector<InstancedSolidItem>& data);
  void draw(const RenderDrawContext& ctx,
	    const RenderLayerState& runtime) const;  
private:
  DiskMeshGL mesh_;
  SolidInstanceBuffers solid_;
};

class CubeRenderer : public InstancedSolidRendererBase {
public:
  void init();
  void destroy();

  void sync(const std::vector<InstancedSolidItem>& data);
  void draw(const RenderDrawContext& ctx,
	    const RenderLayerState& runtime) const;  
private:
  CubeMeshGL mesh_;
  SolidInstanceBuffers solid_;
};

struct LineGpuSegment {
  GLint first = 0;
  GLsizei count = 0;
  glm::vec3 color{1.0f};
  float opacity = 1.0f;
  LinePrimitiveMode mode = LinePrimitiveMode::Strip;
};

class LineRenderer {
public:
  void init();
  void destroy();

  void sync(const std::vector<LineRenderItem>& data);

  void draw(const RenderDrawContext& ctx,
            const RenderLayerState& runtime) const;

private:
  GLuint vao_ = 0;
  GLuint vbo_ = 0;

  std::vector<LineGpuSegment> segments_;
  GLsizei totalVertexCount_ = 0;
};

class CuboidRenderer {
public:
  void init();
  void destroy();

  void sync(const std::vector<CuboidRenderItem>& data);

  void draw(const RenderDrawContext& ctx,
            const RenderLayerState& runtime) const;

private:
  GLuint vao_ = 0;
  GLuint vbo_ = 0;

  std::vector<LineGpuSegment> segments_;
  GLsizei totalVertexCount_ = 0;
};

#ifdef ISO_CONTOUR
struct IsoContourRenderData;
class IsoContourRenderer {
public:
  void sync(const IsoContourRenderData& data);
  void draw(const RenderDrawContext& ctx,
	    const RenderLayerState& runtime) const;  
  void destroy();

private:
  GLuint vao_ = 0, vbo_ = 0, nbo_ = 0, ebo_ = 0;
  GLsizei indexCount_ = 0;
};

#endif
