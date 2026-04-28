#include "render/opengl/field_renderer.h"
#include <vector>
#include <glm/gtc/type_ptr.hpp>

VelocityFieldRenderer gVelocityFieldRenderer;

void VelocityFieldRenderer::init()
{
  if (vao_ != 0) return;

  // shaft + 2 head lines
  // local arrow points along +Z
  const float arrowVertices[] = {
    // shaft
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f,

    // head 1
    0.0f, 0.0f, 1.0f,
    0.08f, 0.0f, 0.82f,

    // head 2
    0.0f, 0.0f, 1.0f,
   -0.08f, 0.0f, 0.82f
  };

  arrowVertexCount_ = 6;

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &arrowVBO_);
  glGenBuffers(1, &instanceVBO_);

  glBindVertexArray(vao_);

  glBindBuffer(GL_ARRAY_BUFFER, arrowVBO_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(arrowVertices), arrowVertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);

  glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
  glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

  // instancePos
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribDivisor(1, 1);

  // instanceVel
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribDivisor(2, 1);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

void VelocityFieldRenderer::destroy()
{
  if (instanceVBO_) glDeleteBuffers(1, &instanceVBO_);
  if (arrowVBO_) glDeleteBuffers(1, &arrowVBO_);
  if (vao_) glDeleteVertexArrays(1, &vao_);

  instanceVBO_ = 0;
  arrowVBO_ = 0;
  vao_ = 0;
  arrowVertexCount_ = 0;
  instanceCount_ = 0;
}

void VelocityFieldRenderer::sync(const std::vector<float>& instanceData)
{
  if (vao_ == 0) return;

  instanceCount_ = static_cast<GLsizei>(instanceData.size() / 6);

  glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
  glBufferData(GL_ARRAY_BUFFER,
               instanceData.size() * sizeof(float),
               instanceData.data(),
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VelocityFieldRenderer::draw(GLuint program,
                                 const glm::mat4& view,
                                 const glm::mat4& projection,
                                 float scaleFactor,
                                 bool useLogScale) const
{
  if (vao_ == 0 || instanceCount_ == 0) return;

  glUseProgram(program);
  glUniformMatrix4fv(glGetUniformLocation(program, "view"),
                     1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(program, "projection"),
                     1, GL_FALSE, glm::value_ptr(projection));
  glUniform1f(glGetUniformLocation(program, "scaleFactor"), scaleFactor);
  glUniform1f(glGetUniformLocation(program, "logScale"), useLogScale ? 1.0f : 0.0f);

  glBindVertexArray(vao_);
  glDrawArraysInstanced(GL_LINES, 0, arrowVertexCount_, instanceCount_);
  glBindVertexArray(0);
}
