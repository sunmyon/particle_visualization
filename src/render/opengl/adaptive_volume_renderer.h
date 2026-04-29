#pragma once

#include <cstdint>
#include <cstddef>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "volume/adaptive_volume_tree.h"

struct AdaptiveVolumeDrawParams {
  glm::mat4 invProjection{1.0f};
  glm::mat4 invView{1.0f};
  glm::vec3 cameraForward{0.0f, 0.0f, -1.0f};
  glm::ivec2 resolution{1, 1};
  float focalPixels = 1.0f;
  float pixelThreshold = 2.0f;
  float tauMax = 1.0f;
  float stepBias = 0.0f;
  float skipEpsilon = 1.0e-4f;
  int debugMode = 0;
};

class AdaptiveVolumeRenderer {
public:
  void init();
  void sync(const AdaptiveVolumeTree& tree);
  void draw(GLuint program, const AdaptiveVolumeDrawParams& params) const;
  void destroy();

  std::uint64_t uploadedVersion() const { return uploadedVersion_; }
  bool hasData() const { return nodeCount_ > 0; }

private:
  void destroyTextureBuffer(GLuint& texture, GLuint& buffer);
  void uploadFloatBuffer(GLuint& texture,
                         GLuint& buffer,
                         GLenum internalFormat,
                         const float* data,
                         std::size_t count);
  void uploadIntBuffer(GLuint& texture,
                       GLuint& buffer,
                       GLenum internalFormat,
                       const int* data,
                       std::size_t count);

  GLuint vao_ = 0;
  GLuint nodeMinTex_ = 0;
  GLuint nodeMinBuffer_ = 0;
  GLuint nodeMaxTex_ = 0;
  GLuint nodeMaxBuffer_ = 0;
  GLuint childATex_ = 0;
  GLuint childABuffer_ = 0;
  GLuint childBTex_ = 0;
  GLuint childBBuffer_ = 0;
  GLuint cornerLoTex_ = 0;
  GLuint cornerLoBuffer_ = 0;
  GLuint cornerHiTex_ = 0;
  GLuint cornerHiBuffer_ = 0;

  int root_ = -1;
  int nodeCount_ = 0;
  std::uint64_t uploadedVersion_ = 0;
};
