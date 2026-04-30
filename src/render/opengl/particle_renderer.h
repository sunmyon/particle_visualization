#pragma once

#include <vector>
#include <cstddef>

#include <glad/glad.h>
#include <glm/glm.hpp>

struct ParticleVisualConfig;
struct RenderParticle;
class ColorbarRenderer;

struct ParticleDrawStyle {
  bool fixedColor = false;
  glm::vec4 color{1.0f, 0.9f, 0.2f, 0.9f};
  float pointScale = 1.0f;
  float alpha = 1.0f;
};

class ParticleRenderer {
public:
  void init();
  void sync(const std::vector<RenderParticle>& P);
  void draw(GLuint particleProgram,
            const glm::mat4& model,
            const glm::mat4& view,
            const glm::mat4& projection,
            const ParticleVisualConfig& visualConfig,
            const ColorbarRenderer& colorbarRenderer,
            const ParticleDrawStyle& style = ParticleDrawStyle{}) const;
  void destroy();

  GLuint vao() const { return vao_; }
  GLuint vbo() const { return vbo_; }
  std::size_t filteredCount() const { return filteredCount_; }    
  
private:
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  std::size_t filteredCount_ = 0;  
};

extern ParticleRenderer gParticleRenderer;
