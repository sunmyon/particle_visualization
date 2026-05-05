#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

struct RenderParticle;

enum class ParticleLodMode : int {
  Off = 0,
  WhileInteracting = 1,
  Always = 2
};

struct ParticleLodSettings {
  ParticleLodMode mode = ParticleLodMode::Off;
  std::uint32_t minNodeParticles = 256;
  std::uint32_t maxDepth = 18;
  float theta = 0.05f;
  float screenPixelThreshold = 5.0f;
  float proxyFraction = 0.8f;
  float focusUpdateDistance = 0.05f;
  float proxyUpdateRateHz = 5.0f;
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

struct ParticleLodGpuTree {
  std::vector<glm::vec4> nodeCenterRadius;
  std::vector<glm::vec4> representativePosHsml;
  std::vector<glm::vec4> representativeValue;
  std::vector<glm::uvec4> nodeMeta; // start, count, depth, child mask
  std::vector<glm::ivec4> childA;
  std::vector<glm::ivec4> childB;
  std::vector<glm::uvec4> representativeMeta; // type, stress flag, parent, reserved
  std::vector<std::uint32_t> indices;
  std::uint32_t maxLeafCount = 0;
  bool valid = false;
};

void BuildParticleLodTree(const std::vector<RenderParticle>& particles,
                          const ParticleLodSettings& settings,
                          ParticleLodTree& out);

void BuildParticleLodGpuTree(const ParticleLodTree& tree,
                             ParticleLodGpuTree& out);

void BuildParticleLodOrderedParticles(
  const std::vector<RenderParticle>& particles,
  const ParticleLodTree& tree,
  std::vector<RenderParticle>& out);

bool BuildParticleLodProxyDrawList(const std::vector<RenderParticle>& particles,
                                   const ParticleLodTree& tree,
                                   const glm::vec3& focus,
                                   const ParticleLodSettings& settings,
                                   std::vector<RenderParticle>& out);

std::size_t EstimateParticleLodTreeBytes(const ParticleLodTree& tree);
std::size_t EstimateParticleLodGpuTreeBytes(const ParticleLodGpuTree& tree);
