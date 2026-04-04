#include "particle_visual_config.h"
#include "object_renderer.h"
#include "object.h"
#include "render_resources.h"

#include <vector>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <unordered_set>

PolyhedronRenderer gPolyhedronRenderer;

void PolyhedronRenderer::requestResetGroup(const std::string& tag)
{
  for (auto& [id, e] : cache_) {
    if (e.tag == tag) {
      if (e.vbo) glDeleteBuffers(1, &e.vbo);
      if (e.vao) glDeleteVertexArrays(1, &e.vao);

      e.vbo = 0;
      e.vao = 0;
      e.vertexCount = 0;
      e.dirty = true;
      e.tag.clear();
    }
  }
}

void PolyhedronRenderer::removeGpuCache(int id)
{
  auto it = cache_.find(id);
  if (it == cache_.end()) return;

  if (it->second.vbo) glDeleteBuffers(1, &it->second.vbo);
  if (it->second.vao) glDeleteVertexArrays(1, &it->second.vao);

  cache_.erase(it);
}

void PolyhedronRenderer::clearGpuCache()
{
  for (auto& [id, e] : cache_) {
    if (e.vbo) glDeleteBuffers(1, &e.vbo);
    if (e.vao) glDeleteVertexArrays(1, &e.vao);
  }
  cache_.clear();
}

void PolyhedronRenderer::sync(const std::vector<PolyhedronRenderItem>& data)
{
  if (resetAllRequested_) {
    clearGpuCache();
    resetAllRequested_ = false;
  }
  
  std::unordered_set<int> aliveIds;
  aliveIds.reserve(data.size());

  for (const auto& item : data) {    
    if (item.id < 0) continue;
    if (item.vertices.empty()) continue;

    aliveIds.insert(item.id);

    auto& e = cache_[item.id];

    if (e.vao == 0) {
      glGenVertexArrays(1, &e.vao);
      glGenBuffers(1, &e.vbo);

      glBindVertexArray(e.vao);
      glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                            sizeof(glm::vec3), (void*)0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glBindVertexArray(0);
    }

    glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 item.vertices.size() * sizeof(glm::vec3),
                 item.vertices.data(),
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    e.vertexCount = static_cast<GLsizei>(item.vertices.size());
    e.color = item.color;
    e.opacity = item.opacity;
    e.tag = item.tag;
  }

  std::vector<int> deadIds;
  deadIds.reserve(cache_.size());

  for (const auto& [id, entry] : cache_) {
    if (!aliveIds.count(id)) {
      deadIds.push_back(id);
    }
  }

  for (int id : deadIds) {
    removeGpuCache(id);
  }
}

void PolyhedronRenderer::draw(const RenderDrawContext& ctx,
                              const RenderLayerState& runtime) const
{  
  if (!runtime.show) return;
  if (cache_.empty()) return;

  glUseProgram(ctx.lineProgram);

  const GLint locView  = glGetUniformLocation(ctx.lineProgram, "view");
  const GLint locProj  = glGetUniformLocation(ctx.lineProgram, "projection");
  const GLint locColor = glGetUniformLocation(ctx.lineProgram, "color");

  glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(ctx.view));
  glUniformMatrix4fv(locProj, 1, GL_FALSE, glm::value_ptr(ctx.projection));

  for (const auto& [id, e] : cache_) {
    if (e.vao == 0 || e.vertexCount == 0) continue;

    glUniform4f(locColor,
                e.color.r, e.color.g, e.color.b,
                e.opacity * runtime.opacity);

    glBindVertexArray(e.vao);
    glDrawArrays(GL_LINES, 0, e.vertexCount);
    glBindVertexArray(0);
  }

  glUseProgram(0);
}

#include <vector>
#include <cstdint>

namespace {

using SphereVtx = SphereMeshGL::Vtx;

void buildSphereMesh(int stacks,
                     int slices,
                     std::vector<SphereVtx>& V,
                     std::vector<uint32_t>& I)
{
  const float PI = 3.1415926535f;

  V.clear();
  I.clear();

  for (int i = 0; i <= stacks; ++i) {
    const float v = float(i) / float(stacks);
    const float phi = PI * (v - 0.5f);
    const float z = std::sin(phi);
    const float r = std::cos(phi);

    for (int j = 0; j <= slices; ++j) {
      const float u = float(j) / float(slices);
      const float theta = 2.0f * PI * u;
      const float x = r * std::cos(theta);
      const float y = r * std::sin(theta);
      const glm::vec3 p(x, y, z);
      V.push_back({p, p});
    }
  }

  for (int i = 0; i < stacks; ++i) {
    for (int j = 0; j < slices; ++j) {
      const int a = i * (slices + 1) + j;
      const int b = (i + 1) * (slices + 1) + j;

      const uint32_t a32 = static_cast<uint32_t>(a);
      const uint32_t b32 = static_cast<uint32_t>(b);

      I.insert(I.end(), {a32, b32, b32 + 1, a32, b32 + 1, a32 + 1});
    }
  }
}

struct MeshData {
  std::vector<glm::vec3> verts;
  std::vector<uint32_t> inds;
};

MeshData buildFlatDiskMesh(int slices = 64)
{
  MeshData m;
  m.verts.reserve(2 + (slices + 1) * 2);
  m.inds.reserve(slices * 12);

  m.verts.push_back({0.f, 0.5f, 0.f});
  m.verts.push_back({0.f,-0.5f, 0.f});

  for (int i = 0; i <= slices; ++i) {
    const float th = 2.f * glm::pi<float>() * float(i) / float(slices);
    const float x = std::cos(th);
    const float z = std::sin(th);
    m.verts.push_back({x,  0.5f, z});
    m.verts.push_back({x, -0.5f, z});
  }

  for (int i = 0; i < slices; ++i) {
    m.inds.insert(m.inds.end(), {0u, 2u + i * 2u, 2u + (i + 1u) * 2u});
    m.inds.insert(m.inds.end(), {1u, 3u + (i + 1u) * 2u, 3u + i * 2u});
  }

  for (int i = 0; i < slices; ++i) {
    const uint32_t a = 2u + i * 2u;
    const uint32_t b = a + 1u;
    const uint32_t c = 2u + (i + 1u) * 2u;
    const uint32_t d = c + 1u;
    m.inds.insert(m.inds.end(), {a, b, c, c, b, d});
  }

  return m;
}

constexpr float cubicVerts[] = {
    -0.5f, -0.5f, -0.5f,
     0.5f, -0.5f, -0.5f,
     0.5f,  0.5f, -0.5f,
    -0.5f,  0.5f, -0.5f,
    -0.5f, -0.5f,  0.5f,
     0.5f, -0.5f,  0.5f,
     0.5f,  0.5f,  0.5f,
    -0.5f,  0.5f,  0.5f
};

constexpr unsigned int cubicIdx[] = {
    0,1,2,  2,3,0,
    4,5,6,  6,7,4,
    4,0,3,  3,7,4,
    1,5,6,  6,2,1,
    4,5,1,  1,0,4,
    3,2,6,  6,7,3
};

} // namespace

void IndexedMeshGL::destroy()
{
  if (ebo_) glDeleteBuffers(1, &ebo_);
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);

  ebo_ = 0;
  vbo_ = 0;
  vao_ = 0;
  indexCount_ = 0;
}

void InstancedSolidRendererBase::initSolidInstanceAttributes_(GLuint vao,
                                                              SolidInstanceBuffers& buffers)
{
  if (buffers.modelVBO == 0)   glGenBuffers(1, &buffers.modelVBO);
  if (buffers.colorVBO == 0)   glGenBuffers(1, &buffers.colorVBO);
  if (buffers.opacityVBO == 0) glGenBuffers(1, &buffers.opacityVBO);

  glBindVertexArray(vao);

  const GLsizei vec4Size = sizeof(glm::vec4);

  glBindBuffer(GL_ARRAY_BUFFER, buffers.modelVBO);
  for (GLuint i = 0; i < 4; ++i) {
    glEnableVertexAttribArray(1 + i);
    glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE,
                          sizeof(glm::mat4),
                          (void*)(uintptr_t(i * vec4Size)));
    glVertexAttribDivisor(1 + i, 1);
  }

  glBindBuffer(GL_ARRAY_BUFFER, buffers.colorVBO);
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE,
                        sizeof(glm::vec3), (void*)0);
  glVertexAttribDivisor(5, 1);

  glBindBuffer(GL_ARRAY_BUFFER, buffers.opacityVBO);
  glEnableVertexAttribArray(6);
  glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE,
                        sizeof(float), (void*)0);
  glVertexAttribDivisor(6, 1);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

void InstancedSolidRendererBase::destroySolidInstanceBuffers_(SolidInstanceBuffers& buffers)
{
  if (buffers.opacityVBO) glDeleteBuffers(1, &buffers.opacityVBO);
  if (buffers.colorVBO)   glDeleteBuffers(1, &buffers.colorVBO);
  if (buffers.modelVBO)   glDeleteBuffers(1, &buffers.modelVBO);

  buffers.opacityVBO = 0;
  buffers.colorVBO = 0;
  buffers.modelVBO = 0;
  buffers.instanceCount = 0;
}

void InstancedSolidRendererBase::syncSolidInstanceBuffers_(const std::vector<InstancedSolidItem>& data,
                                                           SolidInstanceBuffers& buffers)
{
  std::vector<glm::mat4> models;
  std::vector<glm::vec3> colors;
  std::vector<float> opacities;

  models.reserve(data.size());
  colors.reserve(data.size());
  opacities.reserve(data.size());

  for (const auto& item : data) {
    models.push_back(item.model);
    colors.push_back(item.color);
    opacities.push_back(item.opacity);
  }

  glBindBuffer(GL_ARRAY_BUFFER, buffers.modelVBO);
  glBufferData(GL_ARRAY_BUFFER,
               models.size() * sizeof(glm::mat4),
               models.data(),
               GL_DYNAMIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, buffers.colorVBO);
  glBufferData(GL_ARRAY_BUFFER,
               colors.size() * sizeof(glm::vec3),
               colors.data(),
               GL_DYNAMIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, buffers.opacityVBO);
  glBufferData(GL_ARRAY_BUFFER,
               opacities.size() * sizeof(float),
               opacities.data(),
               GL_DYNAMIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, 0);

  buffers.instanceCount = static_cast<GLsizei>(data.size());
}

void InstancedSolidRendererBase::drawSolidInstanced_(GLuint vao,
                                                     GLsizei indexCount,
                                                     GLuint program,
                                                     const glm::mat4& view,
                                                     const glm::mat4& projection,
                                                     const RenderLayerState& runtime,
                                                     GLsizei instanceCount) const
{
  if (!runtime.show) return;
  if (vao == 0 || indexCount == 0 || instanceCount == 0) return;

  glUseProgram(program);

  glUniformMatrix4fv(glGetUniformLocation(program, "view"),
                     1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(program, "projection"),
                     1, GL_FALSE, glm::value_ptr(projection));

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);

  glBindVertexArray(vao);
  glDrawElementsInstanced(GL_TRIANGLES,
                          indexCount,
                          GL_UNSIGNED_INT,
                          nullptr,
                          instanceCount);
  glBindVertexArray(0);

  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

void CubeMeshGL::init()
{
  if (vao_ != 0) return;

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);

  glBindVertexArray(vao_);

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(cubicVerts), cubicVerts, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubicIdx), cubicIdx, GL_STATIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  indexCount_ = 36;
}

void SphereMeshGL::init(int stacks, int slices)
{
  if (vao_ != 0) return;

  std::vector<Vtx> V;
  std::vector<uint32_t> I;
  buildSphereMesh(stacks, slices, V, I);

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);

  glBindVertexArray(vao_);

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               V.size() * sizeof(Vtx),
               V.data(),
               GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               I.size() * sizeof(uint32_t),
               I.data(),
               GL_STATIC_DRAW);

  // location = 0 : position
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                        sizeof(Vtx),
                        (void*)offsetof(Vtx, pos));

  // location = 7 : normal
  // 今の instancedSolid shader では未使用だが、将来用に張っておいてよい
  glEnableVertexAttribArray(7);
  glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE,
                        sizeof(Vtx),
                        (void*)offsetof(Vtx, nrm));

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  indexCount_ = static_cast<GLsizei>(I.size());
}

void DiskMeshGL::init(int slices)
{
  if (vao_ != 0) return;

  const MeshData m = buildFlatDiskMesh(slices);

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);

  glBindVertexArray(vao_);

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               m.verts.size() * sizeof(glm::vec3),
               m.verts.data(),
               GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               m.inds.size() * sizeof(uint32_t),
               m.inds.data(),
               GL_STATIC_DRAW);

  // location = 0 : position
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                        sizeof(glm::vec3),
                        (void*)0);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  indexCount_ = static_cast<GLsizei>(m.inds.size());
}

// =========================
// EllipsoidRenderer
// =========================
void EllipsoidRenderer::init()
{
  mesh_.init();
  if (!mesh_.ready()) return;
  initSolidInstanceAttributes_(mesh_.vao(), solid_);
}

void EllipsoidRenderer::destroy()
{
  destroySolidInstanceBuffers_(solid_);
  mesh_.destroy();
}

void EllipsoidRenderer::sync(const std::vector<InstancedSolidItem>& data)
{
  if (!mesh_.ready()) return;
  syncSolidInstanceBuffers_(data, solid_);
}

void EllipsoidRenderer::draw(const RenderDrawContext& ctx,
                             const RenderLayerState& runtime) const
{
  switch (surfaceMode()) {
  case SurfaceMode::Solid:
  case SurfaceMode::SolidAndWire:
    drawSolidInstanced_(mesh_.vao(),
                        mesh_.indexCount(),
                        ctx.solidProgram,
                        ctx.view,
                        ctx.projection,
                        runtime,
                        solid_.instanceCount);
    break;
  case SurfaceMode::Wire:
    break;
  }
}

// =========================
// DiskRenderer
// =========================
void DiskRenderer::init()
{
  mesh_.init();
  if (!mesh_.ready()) return;
  initSolidInstanceAttributes_(mesh_.vao(), solid_);
}

void DiskRenderer::destroy()
{
  destroySolidInstanceBuffers_(solid_);
  mesh_.destroy();
}

void DiskRenderer::sync(const std::vector<InstancedSolidItem>& data)
{
  if (!mesh_.ready()) return;
  syncSolidInstanceBuffers_(data, solid_);
}

void DiskRenderer::draw(const RenderDrawContext& ctx,
                        const RenderLayerState& runtime) const
{
  switch (surfaceMode()) {
  case SurfaceMode::Solid:
  case SurfaceMode::SolidAndWire:
    drawSolidInstanced_(mesh_.vao(),
                        mesh_.indexCount(),
                        ctx.solidProgram,
                        ctx.view,
                        ctx.projection,
                        runtime,
                        solid_.instanceCount);
    break;
  case SurfaceMode::Wire:
    break;
  }
}

// =========================
// CubeRenderer
// =========================
void CubeRenderer::init()
{
  mesh_.init();
  if (!mesh_.ready()) return;
  initSolidInstanceAttributes_(mesh_.vao(), solid_);
}

void CubeRenderer::destroy()
{
  destroySolidInstanceBuffers_(solid_);
  mesh_.destroy();
}

void CubeRenderer::sync(const std::vector<InstancedSolidItem>& data)
{
  if (!mesh_.ready()) return;
  syncSolidInstanceBuffers_(data, solid_);
}

void CubeRenderer::draw(const RenderDrawContext& ctx,
                        const RenderLayerState& runtime) const
{
  switch (surfaceMode()) {
  case SurfaceMode::Solid:
  case SurfaceMode::SolidAndWire:
    drawSolidInstanced_(mesh_.vao(),
                        mesh_.indexCount(),
                        ctx.solidProgram,
                        ctx.view,
                        ctx.projection,
                        runtime,
                        solid_.instanceCount);
    break;
  case SurfaceMode::Wire:
    break;
  }
}

// =========================
// LineRenderer
// =========================
void LineRenderer::init()
{
  if (vao_ != 0) return;

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

void LineRenderer::destroy()
{
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);

  vbo_ = 0;
  vao_ = 0;
}

void LineRenderer::sync(const std::vector<LineRenderItem>& data)
{
  if (vao_ == 0) return;

  std::vector<glm::vec3> packedVertices;
  segments_.clear();
  totalVertexCount_ = 0;

  size_t totalPoints = 0;
  for (const auto& item : data) {
    totalPoints += item.points.size();
  }

  packedVertices.reserve(totalPoints);
  segments_.reserve(data.size());

  GLint first = 0;

  for (const auto& item : data) {
    if (item.points.empty()) continue;

    LineGpuSegment seg;
    seg.first = first;
    seg.count = static_cast<GLsizei>(item.points.size());
    seg.color = item.color;
    seg.opacity = item.opacity;
    seg.mode = item.mode;

    segments_.push_back(seg);

    packedVertices.insert(packedVertices.end(),
                          item.points.begin(),
                          item.points.end());

    first += seg.count;
  }

  totalVertexCount_ = static_cast<GLsizei>(packedVertices.size());

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               packedVertices.size() * sizeof(glm::vec3),
               packedVertices.data(),
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void LineRenderer::draw(const RenderDrawContext& ctx,
                        const RenderLayerState& runtime) const
{
  if (!runtime.show) return;
  if (vao_ == 0) return;
  if (segments_.empty()) return;

  glUseProgram(ctx.lineProgram);

  glUniformMatrix4fv(glGetUniformLocation(ctx.lineProgram, "model"),
                     1, GL_FALSE, glm::value_ptr(ctx.model));
  glUniformMatrix4fv(glGetUniformLocation(ctx.lineProgram, "view"),
                     1, GL_FALSE, glm::value_ptr(ctx.view));
  glUniformMatrix4fv(glGetUniformLocation(ctx.lineProgram, "projection"),
                     1, GL_FALSE, glm::value_ptr(ctx.projection));

  glBindVertexArray(vao_);

  for (const auto& seg : segments_) {
    glUniform4f(glGetUniformLocation(ctx.lineProgram, "color"),
                seg.color.r, seg.color.g, seg.color.b,
                seg.opacity * runtime.opacity);

    GLenum prim = (seg.mode == LinePrimitiveMode::List)
                    ? GL_LINES
                    : GL_LINE_STRIP;

    glDrawArrays(prim, seg.first, seg.count);
  }

  glBindVertexArray(0);
}

CuboidRenderer gCuboidRenderer;

namespace {

const std::vector<std::pair<int,int>> kAllCuboidEdges = {
  {0,1}, {1,2}, {2,3}, {3,0},
  {4,5}, {5,6}, {6,7}, {7,4},
  {0,4}, {1,5}, {2,6}, {3,7}
};

const std::vector<std::pair<int,int>> kEdgesX = {
  {0,1}, {3,2}, {4,5}, {7,6}
};

const std::vector<std::pair<int,int>> kEdgesY = {
  {1,2}, {0,3}, {5,6}, {4,7}
};

const std::vector<std::pair<int,int>> kEdgesZ = {
  {0,4}, {1,5}, {2,6}, {3,7}
};

const std::vector<std::pair<int,int>>& selectedAxisEdges(CuboidAxis axis)
{
  switch (axis) {
  case CuboidAxis::X: return kEdgesX;
  case CuboidAxis::Y: return kEdgesY;
  case CuboidAxis::Z: return kEdgesZ;
  }
  return kEdgesZ;
}

void appendCuboidEdgeVertices(const CuboidObject& obj,
                              std::vector<glm::vec3>& out)
{
  auto corners = computeCuboidCorners(obj);
  out.reserve(out.size() + kAllCuboidEdges.size() * 2);

  for (const auto& e : kAllCuboidEdges) {
    out.push_back(corners[e.first]);
    out.push_back(corners[e.second]);
  }
}

void appendCuboidHighlightVertices(const CuboidRenderItem& item,
                                   std::vector<glm::vec3>& out)
{
  auto corners = computeCuboidCorners(item.cuboid);
  const auto& edges = selectedAxisEdges(item.selectedAxis);
  out.reserve(out.size() + edges.size() * 2);

  for (const auto& e : edges) {
    out.push_back(corners[e.first]);
    out.push_back(corners[e.second]);
  }
}

} // namespace

void CuboidRenderer::init()
{
  if (vao_ != 0) return;

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                        sizeof(glm::vec3), (void*)0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

void CuboidRenderer::destroy()
{
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);

  vbo_ = 0;
  vao_ = 0;
  segments_.clear();
  totalVertexCount_ = 0;
}

void CuboidRenderer::sync(const std::vector<CuboidRenderItem>& data)
{
  if (vao_ == 0) return;

  std::vector<glm::vec3> packedVertices;
  segments_.clear();
  totalVertexCount_ = 0;

  size_t totalPoints = 0;
  for (const auto& item : data) {
    totalPoints += kAllCuboidEdges.size() * 2;
    if (item.showHighlight) {
      totalPoints += selectedAxisEdges(item.selectedAxis).size() * 2;
    }
  }

  packedVertices.reserve(totalPoints);
  segments_.reserve(data.size() * 2);

  GLint first = 0;

  for (const auto& item : data) {
    {
      const GLint start = first;
      appendCuboidEdgeVertices(item.cuboid, packedVertices);
      const GLsizei count = static_cast<GLsizei>(packedVertices.size() - start);

      if (count > 0) {
        LineGpuSegment seg;
        seg.first = start;
        seg.count = count;
        seg.color = glm::vec3(item.cuboid.edgeColor);
        seg.opacity = item.cuboid.edgeColor.a;
        seg.mode = LinePrimitiveMode::List;
        segments_.push_back(seg);

        first += count;
      }
    }

    if (item.showHighlight) {
      const GLint start = first;
      appendCuboidHighlightVertices(item, packedVertices);
      const GLsizei count = static_cast<GLsizei>(packedVertices.size() - start);

      if (count > 0) {
        LineGpuSegment seg;
        seg.first = start;
        seg.count = count;
        seg.color = glm::vec3(item.highlightColor);
        seg.opacity = item.highlightColor.a;
        seg.mode = LinePrimitiveMode::List;
        segments_.push_back(seg);

        first += count;
      }
    }
  }

  totalVertexCount_ = static_cast<GLsizei>(packedVertices.size());

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               packedVertices.size() * sizeof(glm::vec3),
               packedVertices.data(),
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void CuboidRenderer::draw(const RenderDrawContext& ctx,
                          const RenderLayerState& runtime) const
{
  if (!runtime.show) return;
  if (vao_ == 0) return;
  if (segments_.empty()) return;

  glUseProgram(ctx.lineProgram);

  glUniformMatrix4fv(glGetUniformLocation(ctx.lineProgram, "model"),
                     1, GL_FALSE, glm::value_ptr(ctx.model));
  glUniformMatrix4fv(glGetUniformLocation(ctx.lineProgram, "view"),
                     1, GL_FALSE, glm::value_ptr(ctx.view));
  glUniformMatrix4fv(glGetUniformLocation(ctx.lineProgram, "projection"),
                     1, GL_FALSE, glm::value_ptr(ctx.projection));

  glBindVertexArray(vao_);

  for (const auto& seg : segments_) {
    glUniform4f(glGetUniformLocation(ctx.lineProgram, "color"),
                seg.color.r, seg.color.g, seg.color.b,
                seg.opacity * runtime.opacity);

    GLenum prim = (seg.mode == LinePrimitiveMode::List)
                    ? GL_LINES
                    : GL_LINE_STRIP;

    glDrawArrays(prim, seg.first, seg.count);
  }

  glBindVertexArray(0);
}

#ifdef ISO_CONTOUR
IsoContourRenderer gIsoContourRenderer;

void IsoContourRenderer::sync(const IsoContourRenderData& data)
{
  if (data.verts.empty() || data.inds.empty()) {
    indexCount_ = 0;
    return;
  }

  if (!vao_) {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &nbo_);
    glGenBuffers(1, &ebo_);
  }

  glBindVertexArray(vao_);

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               data.verts.size() * sizeof(float),
               data.verts.data(),
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

  glBindBuffer(GL_ARRAY_BUFFER, nbo_);
  glBufferData(GL_ARRAY_BUFFER,
               data.normals.size() * sizeof(float),
               data.normals.data(),
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               data.inds.size() * sizeof(unsigned),
               data.inds.data(),
               GL_STATIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  indexCount_ = static_cast<GLsizei>(data.inds.size());
}

void IsoContourRenderer::draw(const RenderDrawContext& ctx,
                              const RenderLayerState& runtime) const
{
  if (!runtime.show) return;
  if (vao_ == 0 || indexCount_ == 0) return;
  if (ctx.isoContourProgram == 0) return;

  glUseProgram(ctx.isoContourProgram);

  glUniformMatrix4fv(glGetUniformLocation(ctx.isoContourProgram, "model"),
                     1, GL_FALSE, glm::value_ptr(ctx.model));
  glUniformMatrix4fv(glGetUniformLocation(ctx.isoContourProgram, "view"),
                     1, GL_FALSE, glm::value_ptr(ctx.view));
  glUniformMatrix4fv(glGetUniformLocation(ctx.isoContourProgram, "projection"),
                     1, GL_FALSE, glm::value_ptr(ctx.projection));
  glUniform1f(glGetUniformLocation(ctx.isoContourProgram, "opacity"),
              runtime.opacity);

  glBindVertexArray(vao_);
  glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

void IsoContourRenderer::destroy()
{
  if (ebo_) glDeleteBuffers(1, &ebo_);
  if (nbo_) glDeleteBuffers(1, &nbo_);
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);

  vao_ = 0;
  vbo_ = 0;
  nbo_ = 0;
  ebo_ = 0;
  indexCount_ = 0;
}
#endif
