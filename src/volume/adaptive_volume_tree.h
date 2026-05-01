#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/quantity.h"

struct SimulationBlock;
class ParticleOctree;

using VolumeSigmaMapper = std::function<float(float)>;

struct AdaptiveVolumeTreeNode {
  glm::vec3 boundsMin{0.0f};
  glm::vec3 boundsMax{0.0f};
  float sigmaAvg = 0.0f;
  float sigmaMax = 0.0f;
  std::array<int, 8> child{{-1, -1, -1, -1, -1, -1, -1, -1}};
  std::array<float, 8> cornerSigma{{0.0f, 0.0f, 0.0f, 0.0f,
                                    0.0f, 0.0f, 0.0f, 0.0f}};
  std::uint32_t depth = 0;
  std::uint32_t particleCount = 0;

  bool isLeaf() const;
};

struct AdaptiveVolumeTree {
  std::vector<AdaptiveVolumeTreeNode> nodes;
  int root = -1;
  std::uint64_t version = 1;

  bool valid() const;
  void clear();
};

struct AdaptiveVolumeTreeBuildParams {
  QuantityId quantity = QuantityId::Density;
  std::size_t minParticlesPerLeaf = 32;
  std::size_t maxDepth = 18;
  float emptySigmaEpsilon = 0.0f;
  bool balanceTree = false;
  bool expandBoundsByHsml = true;
  int cornerReconstructionMode = 1; // 0=cell average, 1=shared corners, 2=face gradient.
};

struct AdaptiveVolumeTreeStats {
  std::size_t nodeCount = 0;
  std::size_t leafCount = 0;
  std::size_t emptyNodesDropped = 0;
  std::size_t particleCount = 0;
  std::size_t maxDepth = 0;
  float sigmaMax = 0.0f;
};

struct AdaptiveVolumeTreeBuildResult {
  AdaptiveVolumeTree tree;
  AdaptiveVolumeTreeStats stats;
  std::string warning;
};

AdaptiveVolumeTreeBuildResult BuildAdaptiveVolumeTreeFromOctree(
  ParticleOctree& octree,
  const AdaptiveVolumeTreeBuildParams& params,
  const VolumeSigmaMapper& sigmaMapper);

AdaptiveVolumeTreeBuildResult BuildAdaptiveVolumeTreeFromParticles(
  const SimulationBlock& particles,
  const AdaptiveVolumeTreeBuildParams& params,
  const VolumeSigmaMapper& sigmaMapper);
