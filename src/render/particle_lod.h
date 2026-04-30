#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

struct FrameMatrices;
struct RenderParticle;

enum class ParticleLodMode : int {
  Off = 0,
  WhileInteracting = 1,
  Always = 2
};

struct ParticleLodSettings {
  ParticleLodMode mode = ParticleLodMode::WhileInteracting;
  float pixelThreshold = 3.0f;
  std::uint32_t minNodeParticles = 64;
  std::uint32_t maxDepth = 18;
};

struct ParticleLodBounds {
  glm::vec3 min{0.0f};
  glm::vec3 max{0.0f};
};

struct ParticleLodNode {
  ParticleLodBounds bounds;
  std::uint32_t start = 0;
  std::uint32_t count = 0;
  std::uint32_t depth = 0;
  std::int32_t children[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
  float representativePos[3] = {0.0f, 0.0f, 0.0f};
  std::uint8_t representativeType = 0;
  std::uint8_t representativeFlagStress = 0;
  float representativeHsml = 0.0f;
  float representativeValue = 0.0f;
  float radius = 0.0f;
};

struct ParticleLodTree {
  std::vector<std::uint32_t> indices;
  std::vector<ParticleLodNode> nodes;
  bool valid = false;
};

void BuildParticleLodTree(const std::vector<RenderParticle>& particles,
                          const ParticleLodSettings& settings,
                          ParticleLodTree& out);

void BuildParticleLodDrawList(const ParticleLodTree& tree,
                              const FrameMatrices& frame,
                              const ParticleLodSettings& settings,
                              std::vector<RenderParticle>& out);

std::size_t EstimateParticleLodTreeBytes(const ParticleLodTree& tree);
