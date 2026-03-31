#include "main.h"
#include "particle_renderer.h"
#include "colormap_defs.h"

#include <cstdio>
#include <glm/gtc/type_ptr.hpp>

ParticleRenderer gParticleRenderer;

void ParticleRenderer::init(const ParticleArray& P)
{
  if (vao_ == 0) glGenVertexArrays(1, &vao_);
  if (vbo_ == 0) glGenBuffers(1, &vbo_);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);

  glBufferData(GL_ARRAY_BUFFER,
               P.particleBlock.particles.size() * sizeof(ParticleData),
               P.particleBlock.particles.data(),
               GL_DYNAMIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                        sizeof(ParticleData),
                        (void*)offsetof(ParticleData, pos));
  glEnableVertexAttribArray(0);

  glVertexAttribIPointer(1, 1, GL_UNSIGNED_BYTE,
                         sizeof(ParticleData),
                         (void*)offsetof(ParticleData, type));
  glEnableVertexAttribArray(1);

  glVertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE,
                         sizeof(ParticleData),
                         (void*)offsetof(ParticleData, flag_stress));
  glEnableVertexAttribArray(2);

  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE,
                        sizeof(ParticleData),
                        (void*)offsetof(ParticleData, Hsml));
  glEnableVertexAttribArray(3);

  glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE,
                        sizeof(ParticleData),
                        (void*)offsetof(ParticleData, val_show));
  glEnableVertexAttribArray(4);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

void ParticleRenderer::sync(ParticleArray& P,
                            const ParticleVisualConfig& visualConfig)
{
  if (!P.particlesDirty) return;

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);

  for (int i = 0; i < 6; ++i) {
    for (size_t ipart = 0; ipart < P.particleBlock.particles.size(); ++ipart) {
      auto& p = P.particleBlock.particles[ipart];
      int itype = p.type;
      if (itype != i) continue;

      p.val_show = getScalarValue(P.particleBlock,
                                  p,
                                  ipart,
                                  visualConfig.types[i].selectedQuantity);
    }
  }

  TrackingVector<ParticleData> filtered;
  filtered.reserve(P.particleBlock.particles.size());

  for (size_t i = 0; i < P.particleBlock.particles.size(); ++i) {
    auto& p = P.particleBlock.particles[i];
    if (P.flag_mask[i] == 0 &&
        visualConfig.types[p.type].hideParticles == false) {
      filtered.push_back(p);
    }
  }

  filteredCount_ = filtered.size();

  size_t nStress = 0;
  for (auto& pp : filtered) {
    if (pp.flag_stress == 1) ++nStress;
  }
  static size_t lastStress = (size_t)-1;
  if (nStress != lastStress) {
    std::printf("[stress] filtered=%zu stressed=%zu\n",
                filtered.size(), nStress);
    lastStress = nStress;
  }

  glBufferData(GL_ARRAY_BUFFER,
               filtered.size() * sizeof(ParticleData),
               filtered.data(),
               GL_DYNAMIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, 0);

  P.particlesDirty = false;
  P.velocityDirty = true;
}

void ParticleRenderer::draw(GLuint particleProgram,
                            const glm::mat4& model,
                            const glm::mat4& view,
                            const glm::mat4& projection,
                            const ParticleVisualConfig& visualConfig,
                            const ColorbarRenderer& colorbarRenderer,
                            bool hideAllParticles) const
{
  glUseProgram(particleProgram);

  glUniformMatrix4fv(glGetUniformLocation(particleProgram, "model"),
                     1, GL_FALSE, glm::value_ptr(model));
  glUniformMatrix4fv(glGetUniformLocation(particleProgram, "view"),
                     1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(particleProgram, "projection"),
                     1, GL_FALSE, glm::value_ptr(projection));

  float pointSizes[6];
  float valueMin[6];
  float valueMax[6];
  int useLog[6];
  int samplers[6];
  int periodicMapping[6];

  for (int i = 0; i < 6; ++i) {
    const auto& cfg = visualConfig.types[i];

    pointSizes[i] = cfg.pointSize;
    valueMin[i]   = cfg.colorMin;
    valueMax[i]   = cfg.colorMax;
    useLog[i]     = cfg.useLogScale ? 1 : 0;
    periodicMapping[i] = cfg.periodicColorBar ? 1 : 0;
    samplers[i]   = i;

    int cmap = cfg.colormapIndex;
    if (cmap < 0) cmap = 0;
    if (cmap >= gNumColormaps) cmap = gNumColormaps - 1;

    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_1D, colorbarRenderer.colormapTexture(cmap));
  }

  glUniform1fv(glGetUniformLocation(particleProgram, "pointSizes"), 6, pointSizes);
  glUniform1fv(glGetUniformLocation(particleProgram, "valueMin"),   6, valueMin);
  glUniform1fv(glGetUniformLocation(particleProgram, "valueMax"),   6, valueMax);
  glUniform1iv(glGetUniformLocation(particleProgram, "useLog"),     6, useLog);
  glUniform1iv(glGetUniformLocation(particleProgram, "periodicMapping"), 6, periodicMapping);
  glUniform1iv(glGetUniformLocation(particleProgram, "colormaps"),  6, samplers);

  glBindVertexArray(vao_);
  if (!hideAllParticles) {
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(filteredCount_));
  }
  glBindVertexArray(0);
}

void ParticleRenderer::destroy()
{
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  vbo_ = 0;
  vao_ = 0;
}
