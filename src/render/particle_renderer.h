#pragma once

#include <cstddef>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "object_renderer.h"
#include "gizmo_renderer.h"
#include "particle_visual_config.h"
#include "FileIO/file_io.h"

struct RenderParticle;
class ParticleRenderer {
public:
  void init();
  void sync(const std::vector<RenderParticle>& P);
  void draw(GLuint particleProgram,
            const glm::mat4& model,
            const glm::mat4& view,
            const glm::mat4& projection,
            const ParticleVisualConfig& visualConfig,
            const ColorbarRenderer& colorbarRenderer) const;
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
