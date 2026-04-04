#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

class VelocityFieldRenderer {
public:
  void init();
  void destroy();

  void sync(const std::vector<float>& instanceData);

  void draw(GLuint program,
            const glm::mat4& view,
            const glm::mat4& projection,
            float scaleFactor,
            bool useLogScale) const;

private:
  GLuint vao_ = 0;
  GLuint arrowVBO_ = 0;
  GLuint instanceVBO_ = 0;

  GLsizei arrowVertexCount_ = 0;
  GLsizei instanceCount_ = 0;
};

extern VelocityFieldRenderer gVelocityFieldRenderer;
