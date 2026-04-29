#include "volume/adaptive_volume_tree.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "data/particle_block.h"
#include "data/spatial/particle_octree.h"

namespace {

float SafeSigma(float value, const VolumeSigmaMapper& mapper)
{
  float sigma = mapper ? mapper(value) : value;
  if (!std::isfinite(sigma) || sigma < 0.0f) {
    sigma = 0.0f;
  }
  return sigma;
}

float BoxVolume(const BoundingBox& box)
{
  const glm::vec3 d = glm::max(box.max - box.min, glm::vec3(0.0f));
  return d.x * d.y * d.z;
}

bool IsDroppableEmptyLeaf(const ParticleOctree::Node& node,
                          const AdaptiveVolumeTreeBuildParams& params)
{
  return node.isLeaf &&
         node.count == 0 &&
         params.emptySigmaEpsilon >= 0.0f;
}

struct TreeBuilder {
  ParticleOctree& octree;
  const AdaptiveVolumeTreeBuildParams& params;
  const VolumeSigmaMapper& sigmaMapper;
  AdaptiveVolumeTreeBuildResult result;

  int appendNode(const ParticleOctree::Node& node)
  {
    if (IsDroppableEmptyLeaf(node, params)) {
      ++result.stats.emptyNodesDropped;
      return -1;
    }

    const int my = static_cast<int>(result.tree.nodes.size());
    AdaptiveVolumeTreeNode out;
    out.boundsMin = node.box.min;
    out.boundsMax = node.box.max;
    out.depth = static_cast<std::uint32_t>(node.depth);
    out.particleCount = static_cast<std::uint32_t>(
      std::min<std::size_t>(node.count, std::numeric_limits<std::uint32_t>::max()));
    result.tree.nodes.push_back(out);

    result.stats.maxDepth = std::max(result.stats.maxDepth, node.depth);

    if (node.isLeaf) {
      fillLeaf(node, result.tree.nodes[my]);
      ++result.stats.leafCount;
      result.stats.sigmaMax = std::max(result.stats.sigmaMax,
                                       result.tree.nodes[my].sigmaMax);
      return my;
    }

    AdaptiveVolumeTreeNode updated = result.tree.nodes[my];
    fillInternal(node, updated);
    result.tree.nodes[my] = updated;
    result.stats.sigmaMax = std::max(result.stats.sigmaMax,
                                     result.tree.nodes[my].sigmaMax);
    return my;
  }

  void fillLeaf(const ParticleOctree::Node& node, AdaptiveVolumeTreeNode& out)
  {
    const auto& particles = octree.getParticles();
    float sigmaMax = 0.0f;
    double sigmaSum = 0.0;
    std::size_t finiteCount = 0;

    for (std::size_t i = 0; i < node.count; ++i) {
      const std::size_t ip = node.start + i;
      if (ip >= particles.size()) break;
      const float sigma = SafeSigma(particles[ip].val, sigmaMapper);
      sigmaMax = std::max(sigmaMax, sigma);
      sigmaSum += sigma;
      ++finiteCount;
    }

    out.sigmaAvg = finiteCount > 0
      ? static_cast<float>(sigmaSum / static_cast<double>(finiteCount))
      : 0.0f;
    out.sigmaMax = sigmaMax;
    out.cornerSigma.fill(out.sigmaAvg);
  }

  void fillInternal(const ParticleOctree::Node& node, AdaptiveVolumeTreeNode& out)
  {
    const float nodeVolume = std::max(BoxVolume(node.box), 1.0e-30f);
    double weightedSigma = 0.0;
    float sigmaMax = 0.0f;
    std::array<float, 8> childAvg{};
    childAvg.fill(0.0f);

    for (int c = 0; c < 8; ++c) {
      const auto& child = node.children[c];
      if (!child) continue;

      const int childIndex = appendNode(*child);
      out.child[c] = childIndex;
      if (childIndex < 0) continue;

      const AdaptiveVolumeTreeNode& childNode =
        result.tree.nodes[static_cast<std::size_t>(childIndex)];
      childAvg[c] = childNode.sigmaAvg;
      weightedSigma +=
        static_cast<double>(childNode.sigmaAvg) * BoxVolume(child->box);
      sigmaMax = std::max(sigmaMax, childNode.sigmaMax);
    }

    out.sigmaAvg = static_cast<float>(weightedSigma / nodeVolume);
    out.sigmaMax = sigmaMax;

    // Corner order matches the shader's trilerp8 layout.
    out.cornerSigma[0] = childAvg[0];
    out.cornerSigma[1] = childAvg[1];
    out.cornerSigma[2] = childAvg[3];
    out.cornerSigma[3] = childAvg[2];
    out.cornerSigma[4] = childAvg[4];
    out.cornerSigma[5] = childAvg[5];
    out.cornerSigma[6] = childAvg[7];
    out.cornerSigma[7] = childAvg[6];
  }
};

BoundingBox ComputeParticleBounds(const ParticleBlock& block,
                                  const AdaptiveVolumeTreeBuildParams& params)
{
  BoundingBox bounds;
  bounds.min = glm::vec3(std::numeric_limits<float>::max());
  bounds.max = glm::vec3(-std::numeric_limits<float>::max());

  for (const ParticleData& p : block.particles) {
    const float h = params.expandBoundsByHsml
      ? std::max(p.Hsml, 0.0f)
      : 0.0f;
    const glm::vec3 pos(p.pos[0], p.pos[1], p.pos[2]);
    bounds.min = glm::min(bounds.min, pos - glm::vec3(h));
    bounds.max = glm::max(bounds.max, pos + glm::vec3(h));
  }

  if (bounds.min.x > bounds.max.x) {
    bounds.min = glm::vec3(-1.0f);
    bounds.max = glm::vec3(1.0f);
  }

  const glm::vec3 extent = bounds.max - bounds.min;
  const float pad = std::max({extent.x, extent.y, extent.z, 1.0f}) * 1.0e-5f;
  bounds.min -= glm::vec3(pad);
  bounds.max += glm::vec3(pad);
  return bounds;
}

} // namespace

bool AdaptiveVolumeTreeNode::isLeaf() const
{
  return std::all_of(child.begin(), child.end(), [](int idx) {
    return idx < 0;
  });
}

bool AdaptiveVolumeTree::valid() const
{
  return root >= 0 &&
         root < static_cast<int>(nodes.size()) &&
         !nodes.empty();
}

void AdaptiveVolumeTree::clear()
{
  nodes.clear();
  root = -1;
  ++version;
}

AdaptiveVolumeTreeBuildResult BuildAdaptiveVolumeTreeFromOctree(
  ParticleOctree& octree,
  const AdaptiveVolumeTreeBuildParams& params,
  const VolumeSigmaMapper& sigmaMapper)
{
  if (params.balanceTree) {
    octree.balanceTree(/*isIsoDensity=*/false);
  }

  TreeBuilder builder{octree, params, sigmaMapper, {}};
  builder.result.tree.root = builder.appendNode(octree.root());
  builder.result.stats.nodeCount = builder.result.tree.nodes.size();
  builder.result.stats.particleCount = octree.getParticles().size();

  if (!builder.result.tree.valid()) {
    builder.result.warning = "Adaptive volume tree is empty.";
  }

  return builder.result;
}

AdaptiveVolumeTreeBuildResult BuildAdaptiveVolumeTreeFromParticles(
  const ParticleBlock& particles,
  const AdaptiveVolumeTreeBuildParams& params,
  const VolumeSigmaMapper& sigmaMapper)
{
  TrackingVector<ParticleDataForTree> treeParticles;
  treeParticles.reserve(particles.particles.size());

  for (std::size_t i = 0; i < particles.particles.size(); ++i) {
    const ParticleData& p = particles.particles[i];
    ParticleDataForTree out;
    out.pos = glm::vec3(p.pos[0], p.pos[1], p.pos[2]);
    out.val = getScalarValue(particles,
                             p,
                             static_cast<int>(i),
                             params.quantity);
    treeParticles.push_back(out);
  }

  ParticleOctree octree(std::move(treeParticles),
                        ComputeParticleBounds(particles, params),
                        params.minParticlesPerLeaf,
                        params.maxDepth);

  return BuildAdaptiveVolumeTreeFromOctree(octree, params, sigmaMapper);
}
