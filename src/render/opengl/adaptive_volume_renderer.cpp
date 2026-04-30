#include "render/opengl/adaptive_volume_renderer.h"

#include <algorithm>
#include <vector>

namespace {

void BindTextureBufferUnit(GLuint program,
                           const char* uniformName,
                           GLuint texture,
                           GLint unit)
{
  const GLint loc = glGetUniformLocation(program, uniformName);
  if (loc < 0) return;
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_BUFFER, texture);
  glUniform1i(loc, unit);
}

void SetUniform1i(GLuint program, const char* name, GLint value)
{
  const GLint loc = glGetUniformLocation(program, name);
  if (loc >= 0) glUniform1i(loc, value);
}

void SetUniform1f(GLuint program, const char* name, GLfloat value)
{
  const GLint loc = glGetUniformLocation(program, name);
  if (loc >= 0) glUniform1f(loc, value);
}

void SetUniform1iv(GLuint program,
                   const char* name,
                   GLsizei count,
                   const int* values)
{
  const GLint loc = glGetUniformLocation(program, name);
  if (loc >= 0) glUniform1iv(loc, count, values);
}

void SetUniform1fv(GLuint program,
                   const char* name,
                   GLsizei count,
                   const float* values)
{
  const GLint loc = glGetUniformLocation(program, name);
  if (loc >= 0) glUniform1fv(loc, count, values);
}

void SetUniform2f(GLuint program, const char* name, const glm::ivec2& value)
{
  const GLint loc = glGetUniformLocation(program, name);
  if (loc >= 0) {
    glUniform2f(loc,
                static_cast<float>(std::max(value.x, 1)),
                static_cast<float>(std::max(value.y, 1)));
  }
}

void SetUniform3f(GLuint program, const char* name, const glm::vec3& value)
{
  const GLint loc = glGetUniformLocation(program, name);
  if (loc >= 0) glUniform3f(loc, value.x, value.y, value.z);
}

void SetUniformMatrix4(GLuint program, const char* name, const glm::mat4& value)
{
  const GLint loc = glGetUniformLocation(program, name);
  if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, &value[0][0]);
}

} // namespace

void AdaptiveVolumeRenderer::init()
{
  if (vao_ == 0) {
    glGenVertexArrays(1, &vao_);
  }
}

void AdaptiveVolumeRenderer::destroyTextureBuffer(GLuint& texture, GLuint& buffer)
{
  if (texture != 0) {
    glDeleteTextures(1, &texture);
    texture = 0;
  }
  if (buffer != 0) {
    glDeleteBuffers(1, &buffer);
    buffer = 0;
  }
}

void AdaptiveVolumeRenderer::destroy()
{
  destroyTextureBuffer(nodeMinTex_, nodeMinBuffer_);
  destroyTextureBuffer(nodeMaxTex_, nodeMaxBuffer_);
  destroyTextureBuffer(childATex_, childABuffer_);
  destroyTextureBuffer(childBTex_, childBBuffer_);
  destroyTextureBuffer(cornerLoTex_, cornerLoBuffer_);
  destroyTextureBuffer(cornerHiTex_, cornerHiBuffer_);

  if (vao_ != 0) {
    glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
  }

  root_ = -1;
  nodeCount_ = 0;
  uploadedVersion_ = 0;
}

void AdaptiveVolumeRenderer::uploadFloatBuffer(GLuint& texture,
                                               GLuint& buffer,
                                               GLenum internalFormat,
                                               const float* data,
                                               std::size_t count)
{
  if (texture == 0) glGenTextures(1, &texture);
  if (buffer == 0) glGenBuffers(1, &buffer);

  glBindBuffer(GL_TEXTURE_BUFFER, buffer);
  glBufferData(GL_TEXTURE_BUFFER,
               static_cast<GLsizeiptr>(count * sizeof(float)),
               data,
               GL_STATIC_DRAW);

  glBindTexture(GL_TEXTURE_BUFFER, texture);
  glTexBuffer(GL_TEXTURE_BUFFER, internalFormat, buffer);
}

void AdaptiveVolumeRenderer::uploadIntBuffer(GLuint& texture,
                                             GLuint& buffer,
                                             GLenum internalFormat,
                                             const int* data,
                                             std::size_t count)
{
  if (texture == 0) glGenTextures(1, &texture);
  if (buffer == 0) glGenBuffers(1, &buffer);

  glBindBuffer(GL_TEXTURE_BUFFER, buffer);
  glBufferData(GL_TEXTURE_BUFFER,
               static_cast<GLsizeiptr>(count * sizeof(int)),
               data,
               GL_STATIC_DRAW);

  glBindTexture(GL_TEXTURE_BUFFER, texture);
  glTexBuffer(GL_TEXTURE_BUFFER, internalFormat, buffer);
}

void AdaptiveVolumeRenderer::sync(const AdaptiveVolumeTree& tree)
{
  if (!tree.valid()) {
    root_ = -1;
    nodeCount_ = 0;
    uploadedVersion_ = tree.version;
    return;
  }

  std::vector<float> nodeMin;
  std::vector<float> nodeMax;
  std::vector<int> childA;
  std::vector<int> childB;
  std::vector<float> cornerLo;
  std::vector<float> cornerHi;

  nodeMin.reserve(tree.nodes.size() * 4);
  nodeMax.reserve(tree.nodes.size() * 4);
  childA.reserve(tree.nodes.size() * 4);
  childB.reserve(tree.nodes.size() * 4);
  cornerLo.reserve(tree.nodes.size() * 4);
  cornerHi.reserve(tree.nodes.size() * 4);

  for (const AdaptiveVolumeTreeNode& node : tree.nodes) {
    nodeMin.push_back(node.boundsMin.x);
    nodeMin.push_back(node.boundsMin.y);
    nodeMin.push_back(node.boundsMin.z);
    nodeMin.push_back(node.sigmaAvg);

    nodeMax.push_back(node.boundsMax.x);
    nodeMax.push_back(node.boundsMax.y);
    nodeMax.push_back(node.boundsMax.z);
    nodeMax.push_back(node.sigmaMax);

    for (int i = 0; i < 4; ++i) childA.push_back(node.child[i]);
    for (int i = 4; i < 8; ++i) childB.push_back(node.child[i]);

    for (int i = 0; i < 4; ++i) cornerLo.push_back(node.cornerSigma[i]);
    for (int i = 4; i < 8; ++i) cornerHi.push_back(node.cornerSigma[i]);
  }

  uploadFloatBuffer(nodeMinTex_, nodeMinBuffer_, GL_RGBA32F,
                    nodeMin.data(), nodeMin.size());
  uploadFloatBuffer(nodeMaxTex_, nodeMaxBuffer_, GL_RGBA32F,
                    nodeMax.data(), nodeMax.size());
  uploadIntBuffer(childATex_, childABuffer_, GL_RGBA32I,
                  childA.data(), childA.size());
  uploadIntBuffer(childBTex_, childBBuffer_, GL_RGBA32I,
                  childB.data(), childB.size());
  uploadFloatBuffer(cornerLoTex_, cornerLoBuffer_, GL_RGBA32F,
                    cornerLo.data(), cornerLo.size());
  uploadFloatBuffer(cornerHiTex_, cornerHiBuffer_, GL_RGBA32F,
                    cornerHi.data(), cornerHi.size());

  glBindBuffer(GL_TEXTURE_BUFFER, 0);
  glBindTexture(GL_TEXTURE_BUFFER, 0);

  root_ = tree.root;
  nodeCount_ = static_cast<int>(tree.nodes.size());
  uploadedVersion_ = tree.version;
}

void AdaptiveVolumeRenderer::draw(GLuint program,
                                  const AdaptiveVolumeDrawParams& params) const
{
  if (program == 0 || nodeCount_ <= 0 || root_ < 0) {
    return;
  }

  glUseProgram(program);
  BindTextureBufferUnit(program, "nodeMinTB", nodeMinTex_, 0);
  BindTextureBufferUnit(program, "nodeMaxTB", nodeMaxTex_, 1);
  BindTextureBufferUnit(program, "childATB", childATex_, 2);
  BindTextureBufferUnit(program, "childBTB", childBTex_, 3);
  BindTextureBufferUnit(program, "cornerLoTB", cornerLoTex_, 4);
  BindTextureBufferUnit(program, "cornerHiTB", cornerHiTex_, 5);

  SetUniformMatrix4(program, "invProj", params.invProjection);
  SetUniformMatrix4(program, "invView", params.invView);
  SetUniform3f(program, "uCamForward", params.cameraForward);
  SetUniform1f(program, "uFocalPx", params.focalPixels);
  SetUniform1i(program, "uRoot", root_);
  SetUniform2f(program, "uResolution", params.resolution);
  SetUniform1f(program, "uPxThreshold", params.pixelThreshold);
  SetUniform1f(program, "uTauMax", params.tauMax);
  SetUniform1f(program, "uStepBias", params.stepBias);
  SetUniform1f(program, "uSkipEps", params.skipEpsilon);
  SetUniform1i(program, "uDebugMode", params.debugMode);
  SetUniform3f(program, "uVolumeColor", params.baseColor);
  SetUniform1i(program, "uColorMode", params.colorMode);
  SetUniform1f(program, "uTfValueMin", params.tfValueMin);
  SetUniform1f(program, "uTfValueMax", params.tfValueMax);
  SetUniform1f(program, "uTfSigmaScale", params.tfSigmaScale);
  SetUniform1f(program, "uTfMaxSigma", params.tfMaxSigma);
  SetUniform1i(program, "uTfLogScale", params.tfLogScale ? 1 : 0);
  SetUniform1i(program, "uTfComponentCount", params.tfComponentCount);
  SetUniform1iv(program, "uTfType", 16, params.tfTypes.data());
  SetUniform1iv(program, "uTfLogDomain", 16, params.tfLogDomains.data());
  SetUniform1fv(program, "uTfCenter", 16, params.tfCenters.data());
  SetUniform1fv(program, "uTfWidth", 16, params.tfWidths.data());
  SetUniform1fv(program, "uTfAmp", 16, params.tfAmplitudes.data());

  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glUseProgram(0);
}
