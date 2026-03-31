#include "object_renderer.h"
#include "object.h"

#include <vector>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

void PolyhedronRenderer::drawWireframe(const PolyhedronManager& manager,
                                       const glm::mat4& view,
                                       const glm::mat4& projection,
                                       GLuint lineProgram)
{
  if (resetAllRequested_) {
    clearGpuCache();
    resetAllRequested_ = false;
  }

  if (!manager.show()) return;

  glUseProgram(lineProgram);

  const GLint locView  = glGetUniformLocation(lineProgram, "view");
  const GLint locProj  = glGetUniformLocation(lineProgram, "projection");
  const GLint locColor = glGetUniformLocation(lineProgram, "color");

  glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(locProj, 1, GL_FALSE, glm::value_ptr(projection));

  for (const auto& [id, obj] : manager.getObjects()) {
    if (obj.empty()) continue;
    if (obj.vertices.empty()) continue;

    auto& e = cache_[id];

    if (e.vao == 0) {
      glGenVertexArrays(1, &e.vao);
      glGenBuffers(1, &e.vbo);

      glBindVertexArray(e.vao);
      glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glBindVertexArray(0);

      e.dirty = true;
    }

    if (e.dirty) {
      glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
      glBufferData(GL_ARRAY_BUFFER,
                   obj.vertices.size() * sizeof(glm::vec3),
                   obj.vertices.data(),
                   GL_STATIC_DRAW);
      glBindBuffer(GL_ARRAY_BUFFER, 0);

      e.vertexCount = static_cast<GLsizei>(obj.vertices.size());
      e.tag = obj.tag;
      e.dirty = false;
    }

    glUniform4f(locColor, obj.color.r, obj.color.g, obj.color.b, obj.opacity);

    glBindVertexArray(e.vao);
    glDrawArrays(GL_LINES, 0, e.vertexCount);
    glBindVertexArray(0);
  }

  glUseProgram(0);
}


#include <vector>
#include <cstdint>

EllipsoidRenderer gEllipsoidRenderer;
DiskRenderer      gDiskRenderer;
LineRenderer      gLineRenderer;
CubeRenderer      gCubeRenderer;

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

// =========================
// SphereMeshGL
// =========================
void SphereMeshGL::init(int stacks, int slices)
{
  if (vao_ != 0) return;

  std::vector<Vtx> V;
  std::vector<uint32_t> I;
  buildSphereMesh(stacks, slices, V, I);

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ibo_);

  glBindVertexArray(vao_);

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               V.size() * sizeof(Vtx),
               V.data(),
               GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               I.size() * sizeof(uint32_t),
               I.data(),
               GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0);

  glBindVertexArray(0);

  indexCount_ = static_cast<GLsizei>(I.size());
}

void SphereMeshGL::destroy()
{
  if (ibo_) glDeleteBuffers(1, &ibo_);
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);

  ibo_ = 0;
  vbo_ = 0;
  vao_ = 0;
  indexCount_ = 0;
}

// =========================
// DiskMeshGL
// =========================
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
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               m.inds.size() * sizeof(uint32_t),
               m.inds.data(),
               GL_STATIC_DRAW);

  glBindVertexArray(0);

  indexCount_ = static_cast<GLsizei>(m.inds.size());
}

void DiskMeshGL::destroy()
{
  if (ebo_) glDeleteBuffers(1, &ebo_);
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);

  ebo_ = 0;
  vbo_ = 0;
  vao_ = 0;
  indexCount_ = 0;
}

// =========================
// EllipsoidRenderer
// =========================
void EllipsoidRenderer::init()
{
  sphere_.init();
}

void EllipsoidRenderer::destroy()
{
  sphere_.destroy();
}

void EllipsoidRenderer::draw(const EllipsoidManager& manager,
                             GLuint program,
                             const glm::mat4& view,
                             const glm::mat4& projection,
                             const RenderLayerState& runtime) const
{
  if (!runtime.show) return;
  if (!manager.show() || !sphere_.ready()) return;

  glUseProgram(program);

  glUniformMatrix4fv(glGetUniformLocation(program, "view"),
                     1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(program, "projection"),
                     1, GL_FALSE, glm::value_ptr(projection));

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);

  glBindVertexArray(sphere_.vao());

  for (const auto& e : manager.getEllipsoids()) {
    if (e.renderMode != EllipsoidRenderMode::Solid) continue;

    const glm::mat4 M = e.modelMatrix();

    glUniformMatrix4fv(glGetUniformLocation(program, "model"),
                       1, GL_FALSE, glm::value_ptr(M));
    glUniform3fv(glGetUniformLocation(program, "color"),
                 1, glm::value_ptr(e.color));
    glUniform1f(glGetUniformLocation(program, "opacity"),
                e.opacity * runtime.opacity);

    glDrawElements(GL_TRIANGLES, sphere_.indexCount(), GL_UNSIGNED_INT, 0);
  }

  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

// =========================
// DiskRenderer
// =========================
void DiskRenderer::init()
{
  disk_.init();
}

void DiskRenderer::destroy()
{
  disk_.destroy();
}

void DiskRenderer::draw(const DiskManager& manager,
                        GLuint program,
                        const glm::mat4& view,
                        const glm::mat4& projection,
                        const RenderLayerState& runtime) const
{
  if (!runtime.show) return;
  if (!manager.show() || !disk_.ready()) return;

  glUseProgram(program);

  glUniformMatrix4fv(glGetUniformLocation(program, "view"),
                     1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(program, "projection"),
                     1, GL_FALSE, glm::value_ptr(projection));

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);

  glBindVertexArray(disk_.vao());

  for (const auto& disk : manager.getDisks()) {
    const glm::mat4 M = disk.modelMatrix();

    glUniformMatrix4fv(glGetUniformLocation(program, "model"),
                       1, GL_FALSE, glm::value_ptr(M));

    glUniform3f(glGetUniformLocation(program, "color"),
                disk.color.r, disk.color.g, disk.color.b);

    glUniform1f(glGetUniformLocation(program, "opacity"),
                disk.opacity * runtime.opacity);

    glDrawElements(GL_TRIANGLES, disk_.indexCount(), GL_UNSIGNED_INT, 0);
  }

  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
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

void LineRenderer::draw(const LineManager& manager,
                        GLuint program,
                        const glm::mat4& model,
                        const glm::mat4& view,
                        const glm::mat4& projection,
                        const RenderLayerState& runtime) const
{
  if (!runtime.show) return;
  if (!manager.show() || vao_ == 0) return;

  glUseProgram(program);

  glUniformMatrix4fv(glGetUniformLocation(program, "model"),
                     1, GL_FALSE, glm::value_ptr(model));
  glUniformMatrix4fv(glGetUniformLocation(program, "view"),
                     1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(program, "projection"),
                     1, GL_FALSE, glm::value_ptr(projection));

  glBindVertexArray(vao_);

  for (const auto& line : manager.getLines()) {
    if (line.empty()) continue;

    glUniform3f(glGetUniformLocation(program, "color"),
                line.color.r, line.color.g, line.color.b);
    glUniform1f(glGetUniformLocation(program, "opacity"),
                line.opacity * runtime.opacity);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 line.points.size() * sizeof(glm::vec3),
                 line.points.data(),
                 GL_DYNAMIC_DRAW);

    glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(line.points.size()));
  }

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

// =========================
// CubeRenderer
// =========================
void CubeRenderer::init()
{
  if (vao_ != 0) return;

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);
  glGenBuffers(1, &instanceVBO_);
  glGenBuffers(1, &opacityVBO_);

  glBindVertexArray(vao_);

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(cubicVerts), cubicVerts, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

  glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
  const GLsizei vec4Size = sizeof(glm::vec4);
  for (GLuint i = 0; i < 4; ++i) {
    glEnableVertexAttribArray(1 + i);
    glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE,
                          sizeof(glm::mat4),
                          (void*)(uintptr_t(i * vec4Size)));
    glVertexAttribDivisor(1 + i, 1);
  }

  glBindBuffer(GL_ARRAY_BUFFER, opacityVBO_);
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
  glVertexAttribDivisor(5, 1);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubicIdx), cubicIdx, GL_STATIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

void CubeRenderer::destroy()
{
  if (opacityVBO_)  glDeleteBuffers(1, &opacityVBO_);
  if (instanceVBO_) glDeleteBuffers(1, &instanceVBO_);
  if (ebo_)         glDeleteBuffers(1, &ebo_);
  if (vbo_)         glDeleteBuffers(1, &vbo_);
  if (vao_)         glDeleteVertexArrays(1, &vao_);

  opacityVBO_ = 0;
  instanceVBO_ = 0;
  ebo_ = 0;
  vbo_ = 0;
  vao_ = 0;
  instanceCount_ = 0;
}

void CubeRenderer::sync(const CubeManager& manager,
                        RenderLayerState& runtime)
{
  if (vao_ == 0) return;
  if (!runtime.cpuUpdated) return;

  std::vector<glm::mat4> instanceModels;
  std::vector<float> opacities;

  const auto& cubes = manager.getCubes();
  instanceModels.reserve(cubes.size());
  opacities.reserve(cubes.size());

  for (const auto& c : cubes) {
    glm::mat4 M(1.0f);
    M = glm::translate(M, c.position);
    M = glm::scale(M, glm::vec3(c.size));
    instanceModels.push_back(M);
    opacities.push_back(c.opacity * runtime.opacity);
  }

  glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
  glBufferData(GL_ARRAY_BUFFER,
               instanceModels.size() * sizeof(glm::mat4),
               instanceModels.data(),
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glBindBuffer(GL_ARRAY_BUFFER, opacityVBO_);
  glBufferData(GL_ARRAY_BUFFER,
               opacities.size() * sizeof(float),
               opacities.data(),
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  instanceCount_ = static_cast<GLsizei>(instanceModels.size());
  runtime.cpuUpdated = false;
}

void CubeRenderer::draw(const CubeManager& manager,
                        GLuint program,
                        const glm::mat4& view,
                        const glm::mat4& projection,
                        const RenderLayerState& runtime) const
{
  if (!runtime.show) return;
  if (!manager.show() || vao_ == 0 || instanceCount_ == 0) return;

  glUseProgram(program);
  glUniformMatrix4fv(glGetUniformLocation(program, "view"),
                     1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(program, "projection"),
                     1, GL_FALSE, glm::value_ptr(projection));
  glUniform3f(glGetUniformLocation(program, "color"), 1.0f, 1.0f, 1.0f);

  glBindVertexArray(vao_);
  glDrawElementsInstanced(GL_TRIANGLES,
                          indexCount_,
                          GL_UNSIGNED_INT,
                          nullptr,
                          instanceCount_);
  glBindVertexArray(0);
}

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


CrossGizmoRenderer gCrossGizmoRenderer;
CoordAxesRenderer  gCoordAxesRenderer;

void CrossGizmoRenderer::destroy()
{
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  vbo_ = 0;
  vao_ = 0;
}

void CrossGizmoRenderer::draw(GLuint lineProgram,
                              const glm::mat4& view,
                              const glm::mat4& projection,
                              const glm::vec3& cameraPos,
                              const glm::vec3& cameraTarget,
                              const glm::vec3& cameraUp,
                              float crossSize) const
{
  if (vao_ == 0) return;

  GLboolean wasDepthTest = glIsEnabled(GL_DEPTH_TEST);
  glDisable(GL_DEPTH_TEST);
  glLineWidth(3.0f);

  glm::vec3 forward   = glm::normalize(cameraTarget - cameraPos);
  glm::vec3 rightVec  = glm::normalize(glm::cross(forward, cameraUp));
  glm::vec3 upVec     = glm::normalize(glm::cross(rightVec, cameraUp));
  glm::vec3 upVecNew  = glm::normalize(glm::cross(rightVec, forward));

  glm::vec3 v1 = cameraTarget - (rightVec + upVec) * crossSize;
  glm::vec3 v2 = cameraTarget + (rightVec + upVec) * crossSize;
  glm::vec3 v3 = cameraTarget - (rightVec - upVec) * crossSize;
  glm::vec3 v4 = cameraTarget + (rightVec - upVec) * crossSize;
  glm::vec3 v5 = cameraTarget - upVecNew * crossSize;
  glm::vec3 v6 = cameraTarget + upVecNew * crossSize;

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

  glUseProgram(lineProgram);
  glUniformMatrix4fv(glGetUniformLocation(lineProgram, "view"),
                     1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(lineProgram, "projection"),
                     1, GL_FALSE, glm::value_ptr(projection));
  glUniform4f(glGetUniformLocation(lineProgram, "color"),
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

void CoordAxesRenderer::draw(GLuint coordProgram,
                             const glm::mat4& view) const
{
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

  // world axes を camera basis で右下に表示
  glm::mat4 R_c = glm::mat4(glm::mat3(view));
  glm::mat4 model_axes = T * R_c * S;

  glUseProgram(coordProgram);

  GLint locModel      = glGetUniformLocation(coordProgram, "model");
  GLint locView       = glGetUniformLocation(coordProgram, "view");
  GLint locProjection = glGetUniformLocation(coordProgram, "projection");

  glUniformMatrix4fv(locModel,      1, GL_FALSE, glm::value_ptr(model_axes));
  glUniformMatrix4fv(locView,       1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
  glUniformMatrix4fv(locProjection, 1, GL_FALSE, glm::value_ptr(P_ortho));

  glBindVertexArray(vao_);
  glDrawArrays(GL_LINES, 0, 6);
  glBindVertexArray(0);

  glDepthMask(GL_TRUE);
  if (wasDepthTest) glEnable(GL_DEPTH_TEST);
}

ColorbarRenderer      gColorbarRenderer;

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
GLuint CreateColormapTexture1D(const float* rgb, int nColors)
{
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_1D, tex);

  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glTexImage1D(GL_TEXTURE_1D,
               0,
               GL_RGB32F,
               nColors,
               0,
               GL_RGB,
               GL_FLOAT,
               rgb);

  glBindTexture(GL_TEXTURE_1D, 0);
  return tex;
}
}

void ColorbarRenderer::initColorMaps(const ColormapDefView* defs, int numColormaps)
{
  for (GLuint tex : colormapTextures_) {
    if (tex) glDeleteTextures(1, &tex);
  }
  colormapTextures_.clear();

  if (!defs || numColormaps <= 0) return;

  colormapTextures_.reserve(numColormaps);
  for (int i = 0; i < numColormaps; ++i) {
    colormapTextures_.push_back(CreateColormapTexture1D(defs[i].data, defs[i].count));
  }
}

void ColorbarRenderer::destroy()
{
  for (GLuint tex : colormapTextures_) {
    if (tex) glDeleteTextures(1, &tex);
  }
  colormapTextures_.clear();

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

void ColorbarRenderer::updateVertices_(const ColorbarGizmo& gizmo) const
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

void ColorbarRenderer::draw(GLuint colorbarProgram,
                            const ColorbarGizmo& gizmo) const
{
  if (!gizmo.visible) return;
  if (vao_ == 0) return;
  if (gizmo.colormapIndex < 0) return;
  if (static_cast<size_t>(gizmo.colormapIndex) >= colormapTextures_.size()) return;

  updateVertices_(gizmo);

  GLboolean wasDepthTest = glIsEnabled(GL_DEPTH_TEST);
  glDisable(GL_DEPTH_TEST);

  glUseProgram(colorbarProgram);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_1D, colormapTextures_[gizmo.colormapIndex]);
  glUniform1i(glGetUniformLocation(colorbarProgram, "colormap"), 0);

  glBindVertexArray(vao_);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

  glBindTexture(GL_TEXTURE_1D, 0);

  if (wasDepthTest) glEnable(GL_DEPTH_TEST);
}

CuboidRenderer gCuboidRenderer;
ArrowRenderer  gArrowRenderer;

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

  std::vector<glm::vec3> buildCuboidEdgeVertices(const CuboidObject& obj)
  {
    auto corners = computeCuboidCorners(obj);

    std::vector<glm::vec3> verts;
    verts.reserve(kAllCuboidEdges.size() * 2);

    for (const auto& e : kAllCuboidEdges) {
      verts.push_back(corners[e.first]);
      verts.push_back(corners[e.second]);
    }
    return verts;
  }

  std::vector<glm::vec3> buildCuboidHighlightVertices(const CuboidAnnotationObject& obj)
  {
    auto corners = computeCuboidCorners(obj.cuboid);

    std::vector<glm::vec3> verts;
    const auto& edges = selectedAxisEdges(obj.selectedAxis);
    verts.reserve(edges.size() * 2);

    for (const auto& e : edges) {
      verts.push_back(corners[e.first]);
      verts.push_back(corners[e.second]);
    }
    return verts;
  }

  glm::vec3 selectedAxisDirection(const CuboidAnnotationObject& obj)
  {
    if (obj.selectedAxis == CuboidAxis::X)
      return glm::normalize(obj.cuboid.orientation * glm::vec3(1.0f, 0.0f, 0.0f));

    if (obj.selectedAxis == CuboidAxis::Y)
      return glm::normalize(obj.cuboid.orientation * glm::vec3(0.0f, 1.0f, 0.0f));

    return glm::normalize(obj.cuboid.orientation * glm::vec3(0.0f, 0.0f, 1.0f));
  }
  
  void drawLineVertices(const std::vector<glm::vec3>& verts,
			const glm::vec4& color,
			GLuint lineProgram,
			const glm::mat4& view,
			const glm::mat4& projection)
  {
    if (verts.empty()) return;

    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
		 verts.size() * sizeof(glm::vec3),
		 verts.data(),
		 GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    glUseProgram(lineProgram);
    glUniformMatrix4fv(glGetUniformLocation(lineProgram, "view"),
		       1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(lineProgram, "projection"),
		       1, GL_FALSE, glm::value_ptr(projection));
    glUniform4f(glGetUniformLocation(lineProgram, "color"),
		color.r, color.g, color.b, color.a);

    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size()));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
  }

  std::vector<glm::vec3> buildArrowLineVertices(const ArrowObject& obj)
  {
    std::vector<glm::vec3> verts;
    if (obj.empty()) return verts;

    const glm::vec3 dir = glm::normalize(obj.direction);
    const glm::vec3 tip = obj.origin + dir * obj.length;
    const glm::vec3 base = tip - dir * obj.headLength;

    glm::vec3 arbitrary(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(dir, arbitrary)) > 0.9f)
      arbitrary = glm::vec3(1.0f, 0.0f, 0.0f);

    const glm::vec3 u = glm::normalize(glm::cross(dir, arbitrary));
    const glm::vec3 side1 = base + u * obj.headWidth;
    const glm::vec3 side2 = base - u * obj.headWidth;

    verts.reserve(6);
    verts.push_back(obj.origin);
    verts.push_back(tip);

    verts.push_back(tip);
    verts.push_back(side1);

    verts.push_back(tip);
    verts.push_back(side2);

    return verts;
  }

#ifdef ISO_CONTOUR
  void computeIsoContourNormals(const TrackingVector<float>& verts,
				const TrackingVector<unsigned>& inds,
				TrackingVector<float>& out_normals)
  {
    const size_t vcount = verts.size() / 3;
    std::vector<glm::vec3> normals(vcount, glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < inds.size(); i += 3) {
      unsigned i0 = inds[i + 0];
      unsigned i1 = inds[i + 1];
      unsigned i2 = inds[i + 2];

      glm::vec3 v0(verts[3 * i0 + 0], verts[3 * i0 + 1], verts[3 * i0 + 2]);
      glm::vec3 v1(verts[3 * i1 + 0], verts[3 * i1 + 1], verts[3 * i1 + 2]);
      glm::vec3 v2(verts[3 * i2 + 0], verts[3 * i2 + 1], verts[3 * i2 + 2]);

      glm::vec3 n = glm::cross(v1 - v0, v2 - v0);
      if (glm::length(n) > 1.0e-6f)
	n = glm::normalize(n);

      normals[i0] += n;
      normals[i1] += n;
      normals[i2] += n;
    }

    out_normals.resize(verts.size());
    for (size_t v = 0; v < vcount; ++v) {
      glm::vec3 n = normals[v];
      if (glm::length(n) > 1.0e-6f)
	n = glm::normalize(n);
      else
	n = glm::vec3(0.0f, 0.0f, 1.0f);

      out_normals[3 * v + 0] = n.x;
      out_normals[3 * v + 1] = n.y;
      out_normals[3 * v + 2] = n.z;
    }
  }
#endif
  
} // namespace

ArrowObject buildArrowFromCuboidAnnotation(const CuboidAnnotationObject& obj)
{
  ArrowObject arrow;
  arrow.color      = obj.arrowColor;
  arrow.length     = obj.arrowLength;
  arrow.headLength = obj.arrowHeadLength;
  arrow.headWidth  = obj.arrowHeadWidth;
  arrow.tag        = obj.tag;

  const glm::mat3 R = glm::mat3_cast(obj.cuboid.orientation);

  glm::vec3 axisLocal(0.0f);
  float extent = obj.cuboid.halfSize.z;

  if (obj.selectedAxis == CuboidAxis::X) {
    axisLocal = glm::vec3(1.0f, 0.0f, 0.0f);
    extent = obj.cuboid.halfSize.x;
  } else if (obj.selectedAxis == CuboidAxis::Y) {
    axisLocal = glm::vec3(0.0f, 1.0f, 0.0f);
    extent = obj.cuboid.halfSize.y;
  } else {
    axisLocal = glm::vec3(0.0f, 0.0f, 1.0f);
    extent = obj.cuboid.halfSize.z;
  }

  glm::vec3 axisWorld = glm::normalize(R * axisLocal);
  glm::vec3 faceCenter = obj.cuboid.center + axisWorld * extent;

  arrow.origin = faceCenter;
  arrow.direction = axisWorld;
  return arrow;
}
  
void CuboidRenderer::draw(const CuboidObject& obj,
                          GLuint lineProgram,
                          const glm::mat4& view,
                          const glm::mat4& projection,
                          const RenderLayerState& runtime) const
{
  if (!runtime.show) return;
  auto verts = buildCuboidEdgeVertices(obj);
  drawLineVertices(verts, obj.edgeColor, lineProgram, view, projection);
}

void CuboidRenderer::drawHighlight(const CuboidAnnotationObject& obj,
                                   GLuint lineProgram,
                                   const glm::mat4& view,
                                   const glm::mat4& projection,
                                   const RenderLayerState& runtime) const
{
  if (!runtime.show) return;
  if (!obj.showAxisHighlight) return;

  auto verts = buildCuboidHighlightVertices(obj);
  drawLineVertices(verts, obj.highlightColor, lineProgram, view, projection);
}

void ArrowRenderer::draw(const ArrowObject& obj,
                         GLuint lineProgram,
                         const glm::mat4& view,
                         const glm::mat4& projection,
                         const RenderLayerState& runtime) const
{
  if (!runtime.show) return;

  auto verts = buildArrowLineVertices(obj);
  drawLineVertices(verts, obj.color, lineProgram, view, projection);
}

#ifdef ISO_CONTOUR
IsoContourRenderer gIsoContourRenderer;

void IsoContourRenderer::sync(const TrackingVector<float>& verts,
                              const TrackingVector<unsigned>& inds,
                              RenderLayerState& runtime)
{
  if (!runtime.cpuUpdated) return;

  if (verts.empty() || inds.empty()) {
    indexCount_ = 0;
    runtime.cpuUpdated = false;
    return;
  }

  TrackingVector<float> normals;
  computeIsoContourNormals(verts, inds, normals);

  if (!vao_) {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &nbo_);
    glGenBuffers(1, &ebo_);
  }

  glBindVertexArray(vao_);

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               verts.size() * sizeof(float),
               verts.data(),
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

  glBindBuffer(GL_ARRAY_BUFFER, nbo_);
  glBufferData(GL_ARRAY_BUFFER,
               normals.size() * sizeof(float),
               normals.data(),
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               inds.size() * sizeof(unsigned),
               inds.data(),
               GL_STATIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  indexCount_ = static_cast<GLsizei>(inds.size());
  runtime.cpuUpdated = false;
}

void IsoContourRenderer::draw(GLuint program,
                              const glm::mat4& model,
                              const glm::mat4& view,
                              const glm::mat4& projection,
                              const RenderLayerState& runtime) const
{
  if (!runtime.show) return;
  if (vao_ == 0 || indexCount_ == 0) return;

  glUseProgram(program);
  glUniformMatrix4fv(glGetUniformLocation(program, "model"),
                     1, GL_FALSE, glm::value_ptr(model));
  glUniformMatrix4fv(glGetUniformLocation(program, "view"),
                     1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(program, "projection"),
                     1, GL_FALSE, glm::value_ptr(projection));
  glUniform1f(glGetUniformLocation(program, "opacity"),
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
