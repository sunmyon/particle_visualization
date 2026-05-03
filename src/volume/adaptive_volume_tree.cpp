#include "volume/adaptive_volume_tree.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>

#include "data/simulation_block.h"
#include "data/sample_coordinates.h"
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

std::array<glm::vec3, 8> BoxCorners(const BoundingBox& box)
{
  return {{
    {box.min.x, box.min.y, box.min.z},
    {box.max.x, box.min.y, box.min.z},
    {box.max.x, box.max.y, box.min.z},
    {box.min.x, box.max.y, box.min.z},
    {box.min.x, box.min.y, box.max.z},
    {box.max.x, box.min.y, box.max.z},
    {box.max.x, box.max.y, box.max.z},
    {box.min.x, box.max.y, box.max.z},
  }};
}

std::uint32_t FloatBits(float value)
{
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

struct CornerKey {
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t z = 0;

  bool operator==(const CornerKey& other) const
  {
    return x == other.x && y == other.y && z == other.z;
  }
};

struct CornerKeyHash {
  std::size_t operator()(const CornerKey& key) const
  {
    std::size_t h = static_cast<std::size_t>(key.x);
    h ^= static_cast<std::size_t>(key.y) + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= static_cast<std::size_t>(key.z) + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
  }
};

struct CellKey {
  std::uint32_t level = 0;
  std::uint32_t ix = 0;
  std::uint32_t iy = 0;
  std::uint32_t iz = 0;

  bool operator==(const CellKey& other) const
  {
    return level == other.level &&
           ix == other.ix &&
           iy == other.iy &&
           iz == other.iz;
  }
};

struct CellKeyHash {
  std::size_t operator()(const CellKey& key) const
  {
    std::size_t h = static_cast<std::size_t>(key.level);
    h ^= static_cast<std::size_t>(key.ix) + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= static_cast<std::size_t>(key.iy) + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= static_cast<std::size_t>(key.iz) + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
  }
};

CornerKey MakeCornerKey(const glm::vec3& p)
{
  return {FloatBits(p.x), FloatBits(p.y), FloatBits(p.z)};
}

class SharedCornerSampler {
public:
  SharedCornerSampler(ParticleOctree& octree,
                      const VolumeSigmaMapper& mapper,
                      int reconstructionMode)
    : octree_(octree),
      mapper_(mapper),
      reconstructionMode_(std::clamp(reconstructionMode, 1, 2))
  {
    rootMin_ = octree_.root().box.min;
    rootExtent_ = glm::max(octree_.root().box.max - octree_.root().box.min,
                           glm::vec3(1.0e-30f));
    buildLeafIndex(octree_.root());
  }

  float sample(const ParticleOctree::Node& node,
               const glm::vec3& p,
               float fallback)
  {
    const CornerKey key = MakeCornerKey(p);
    const auto found = cache_.find(key);
    if (found != cache_.end()) {
      return found->second;
    }

    std::vector<std::size_t> candidates;
    gatherCornerCandidates(p, candidates);
    if (candidates.empty()) {
      const float value = sampleFromLeafParticles(node, p, fallback);
      cache_.emplace(key, value);
      return value;
    }

    double weightedValue = 0.0;
    double weightSum = 0.0;
    for (std::size_t index : candidates) {
      const LeafSummary& leaf = leaves_[index];
      const glm::vec3 d = leaf.center - p;
      const float dist2 =
        glm::dot(d, d) + std::max(leaf.diag2 * 1.0e-4f, 1.0e-30f);
      const double weight = 1.0 / static_cast<double>(dist2);
      const float value = reconstructionMode_ >= 2
        ? reconstructLeafValue(index, p)
        : leaf.value;
      weightedValue += weight * static_cast<double>(value);
      weightSum += weight;
    }

    const float value = weightSum > 0.0
      ? static_cast<float>(weightedValue / weightSum)
      : fallback;
    cache_.emplace(key, value);
    return value;
  }

private:
  struct LeafSummary {
    const ParticleOctree::Node* node = nullptr;
    glm::vec3 boxMin{0.0f};
    glm::vec3 boxMax{0.0f};
    glm::vec3 center{0.0f};
    float value = 0.0f;
    float diag2 = 1.0f;
    glm::vec3 gradient{0.0f};
    float limiterMin = 0.0f;
    float limiterMax = 0.0f;
    bool gradientReady = false;
    std::uint32_t level = 0;
    std::uint32_t ix = 0;
    std::uint32_t iy = 0;
    std::uint32_t iz = 0;
  };

  static std::uint32_t ClampCellCoord(std::int64_t coord, std::uint32_t level)
  {
    const std::uint64_t cells = 1ull << level;
    if (coord < 0) return 0;
    if (static_cast<std::uint64_t>(coord) >= cells) {
      return static_cast<std::uint32_t>(cells - 1ull);
    }
    return static_cast<std::uint32_t>(coord);
  }

  std::uint32_t CoordFromMin(float value, float rootMin, float rootExtent,
                             std::uint32_t level) const
  {
    const double cells = static_cast<double>(1ull << level);
    const double norm =
      (static_cast<double>(value) - static_cast<double>(rootMin)) /
      static_cast<double>(rootExtent);
    return ClampCellCoord(static_cast<std::int64_t>(std::llround(norm * cells)),
                          level);
  }

  std::array<std::uint32_t, 2> CandidateCoords(float value,
                                               float rootMin,
                                               float rootExtent,
                                               std::uint32_t level) const
  {
    const double cells = static_cast<double>(1ull << level);
    const double norm =
      (static_cast<double>(value) - static_cast<double>(rootMin)) /
      static_cast<double>(rootExtent);
    const std::int64_t hi =
      static_cast<std::int64_t>(std::floor(norm * cells));
    return {{
      ClampCellCoord(hi - 1, level),
      ClampCellCoord(hi, level),
    }};
  }

  float computeLeafAverage(const ParticleOctree::Node& node) const
  {
    const auto& particles = octree_.getParticles();
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < node.count; ++i) {
      const std::size_t ip = node.start + i;
      if (ip >= particles.size()) break;
      sum += SafeSigma(particles[ip].val, mapper_);
      ++count;
    }
    return count > 0 ? static_cast<float>(sum / static_cast<double>(count))
                     : 0.0f;
  }

  static bool AxisOverlap(float a0, float a1, float b0, float b1)
  {
    constexpr float eps = 1.0e-6f;
    return a1 > b0 + eps && b1 > a0 + eps;
  }

  static float AxisOverlapLength(float a0, float a1, float b0, float b1)
  {
    return std::max(0.0f, std::min(a1, b1) - std::max(a0, b0));
  }

  static bool TouchesFace(const LeafSummary& leaf,
                          const LeafSummary& neighbor,
                          int axis,
                          int side)
  {
    constexpr float eps = 1.0e-5f;
    if (side > 0) {
      if (std::abs(neighbor.boxMin[axis] - leaf.boxMax[axis]) > eps) {
        return false;
      }
    } else {
      if (std::abs(neighbor.boxMax[axis] - leaf.boxMin[axis]) > eps) {
        return false;
      }
    }

    const int a0 = (axis + 1) % 3;
    const int a1 = (axis + 2) % 3;
    return AxisOverlap(leaf.boxMin[a0], leaf.boxMax[a0],
                       neighbor.boxMin[a0], neighbor.boxMax[a0]) &&
           AxisOverlap(leaf.boxMin[a1], leaf.boxMax[a1],
                       neighbor.boxMin[a1], neighbor.boxMax[a1]);
  }

  void appendUniqueCandidate(std::vector<std::size_t>& out,
                             std::size_t index) const
  {
    if (std::find(out.begin(), out.end(), index) == out.end()) {
      out.push_back(index);
    }
  }

  void buildLeafIndex(const ParticleOctree::Node& node)
  {
    if (!node.isLeaf) {
      for (const auto& child : node.children) {
        if (child) {
          buildLeafIndex(*child);
        }
      }
      return;
    }

    LeafSummary leaf;
    leaf.node = &node;
    leaf.boxMin = node.box.min;
    leaf.boxMax = node.box.max;
    leaf.center = 0.5f * (node.box.min + node.box.max);
    leaf.value = computeLeafAverage(node);
    leaf.limiterMin = leaf.value;
    leaf.limiterMax = leaf.value;
    const glm::vec3 extent = glm::max(node.box.max - node.box.min,
                                      glm::vec3(0.0f));
    leaf.diag2 = std::max(glm::dot(extent, extent), 1.0e-30f);
    leaf.level = static_cast<std::uint32_t>(
      std::min<std::size_t>(node.depth, 24));
    leaf.ix = CoordFromMin(node.box.min.x, rootMin_.x, rootExtent_.x, leaf.level);
    leaf.iy = CoordFromMin(node.box.min.y, rootMin_.y, rootExtent_.y, leaf.level);
    leaf.iz = CoordFromMin(node.box.min.z, rootMin_.z, rootExtent_.z, leaf.level);

    const std::size_t index = leaves_.size();
    leaves_.push_back(leaf);
    leafByCell_[CellKey{leaf.level, leaf.ix, leaf.iy, leaf.iz}] = index;
    if (std::find(levels_.begin(), levels_.end(), leaf.level) == levels_.end()) {
      levels_.push_back(leaf.level);
    }
  }

  void gatherOverlappingFaceNeighbors(std::size_t leafIndex,
                                      int axis,
                                      int side,
                                      std::vector<std::size_t>& neighbors) const
  {
    neighbors.clear();
    const LeafSummary& leaf = leaves_[leafIndex];
    const float faceCoord = side > 0 ? leaf.boxMax[axis] : leaf.boxMin[axis];

    for (std::uint32_t level : levels_) {
      const double cells = static_cast<double>(1ull << level);
      const double faceNorm =
        (static_cast<double>(faceCoord) -
         static_cast<double>(rootMin_[axis])) /
        static_cast<double>(rootExtent_[axis]);
      const std::int64_t faceCell = side > 0
        ? static_cast<std::int64_t>(std::floor(faceNorm * cells))
        : static_cast<std::int64_t>(std::floor(faceNorm * cells)) - 1;
      if (faceCell < 0 || static_cast<std::uint64_t>(faceCell) >=
                            (1ull << level)) {
        continue;
      }

      const int a0 = (axis + 1) % 3;
      const int a1 = (axis + 2) % 3;
      const auto rangeForAxis = [&](int a) {
        const double lo =
          (static_cast<double>(leaf.boxMin[a]) -
           static_cast<double>(rootMin_[a])) /
          static_cast<double>(rootExtent_[a]);
        const double hi =
          (static_cast<double>(leaf.boxMax[a]) -
           static_cast<double>(rootMin_[a])) /
          static_cast<double>(rootExtent_[a]);
        const std::int64_t ilo =
          static_cast<std::int64_t>(std::floor(lo * cells));
        const std::int64_t ihi =
          static_cast<std::int64_t>(std::ceil(hi * cells)) - 1;
        return std::array<std::uint32_t, 2>{{
          ClampCellCoord(ilo, level),
          ClampCellCoord(ihi, level),
        }};
      };

      const auto r0 = rangeForAxis(a0);
      const auto r1 = rangeForAxis(a1);
      constexpr std::uint32_t kMaxAxisSamples = 16;
      const auto coordAt = [](std::uint32_t lo,
                              std::uint32_t hi,
                              std::uint32_t sample,
                              std::uint32_t sampleCount) {
        if (sampleCount <= 1 || lo >= hi) return lo;
        const double t = static_cast<double>(sample) /
                         static_cast<double>(sampleCount - 1);
        return static_cast<std::uint32_t>(
          std::llround((1.0 - t) * static_cast<double>(lo) +
                       t * static_cast<double>(hi)));
      };
      const std::uint32_t n0 =
        std::min(kMaxAxisSamples, r0[1] >= r0[0] ? r0[1] - r0[0] + 1 : 1);
      const std::uint32_t n1 =
        std::min(kMaxAxisSamples, r1[1] >= r1[0] ? r1[1] - r1[0] + 1 : 1);
      for (std::uint32_t s0 = 0; s0 < n0; ++s0) {
        const std::uint32_t i0 = coordAt(r0[0], r0[1], s0, n0);
        for (std::uint32_t s1 = 0; s1 < n1; ++s1) {
          const std::uint32_t i1 = coordAt(r1[0], r1[1], s1, n1);
          std::array<std::uint32_t, 3> coord{};
          coord[axis] = static_cast<std::uint32_t>(faceCell);
          coord[a0] = i0;
          coord[a1] = i1;
          const auto found =
            leafByCell_.find(CellKey{level, coord[0], coord[1], coord[2]});
          if (found == leafByCell_.end() || found->second == leafIndex) {
            continue;
          }
          if (TouchesFace(leaf, leaves_[found->second], axis, side)) {
            appendUniqueCandidate(neighbors, found->second);
          }
        }
      }
    }
  }

  bool estimateFaceValue(std::size_t leafIndex,
                         int axis,
                         int side,
                         float& value,
                         float& centerDistance)
  {
    std::vector<std::size_t> neighbors;
    gatherOverlappingFaceNeighbors(leafIndex, axis, side, neighbors);
    if (neighbors.empty()) {
      return false;
    }

    const LeafSummary& leaf = leaves_[leafIndex];
    const int a0 = (axis + 1) % 3;
    const int a1 = (axis + 2) % 3;
    double weightedValue = 0.0;
    double weightedDistance = 0.0;
    double weightSum = 0.0;
    for (std::size_t neighborIndex : neighbors) {
      const LeafSummary& neighbor = leaves_[neighborIndex];
      const float overlap0 =
        AxisOverlapLength(leaf.boxMin[a0], leaf.boxMax[a0],
                          neighbor.boxMin[a0], neighbor.boxMax[a0]);
      const float overlap1 =
        AxisOverlapLength(leaf.boxMin[a1], leaf.boxMax[a1],
                          neighbor.boxMin[a1], neighbor.boxMax[a1]);
      const double area =
        std::max(static_cast<double>(overlap0) *
                   static_cast<double>(overlap1),
                 1.0e-30);
      const double distance =
        std::max(std::abs(static_cast<double>(neighbor.center[axis]) -
                          static_cast<double>(leaf.center[axis])),
                 1.0e-30);
      const double weight = area / distance;
      weightedValue += weight * static_cast<double>(neighbor.value);
      weightedDistance += weight * distance;
      weightSum += weight;
      leaves_[leafIndex].limiterMin =
        std::min(leaves_[leafIndex].limiterMin, neighbor.value);
      leaves_[leafIndex].limiterMax =
        std::max(leaves_[leafIndex].limiterMax, neighbor.value);
    }

    value = static_cast<float>(weightedValue / weightSum);
    centerDistance = static_cast<float>(weightedDistance / weightSum);
    return true;
  }

  void ensureGradient(std::size_t leafIndex)
  {
    LeafSummary& leaf = leaves_[leafIndex];
    if (leaf.gradientReady) {
      return;
    }

    leaf.gradient = glm::vec3(0.0f);
    leaf.limiterMin = leaf.value;
    leaf.limiterMax = leaf.value;
    for (int axis = 0; axis < 3; ++axis) {
      float minusValue = leaf.value;
      float plusValue = leaf.value;
      float minusDistance = 0.0f;
      float plusDistance = 0.0f;
      const bool hasMinus =
        estimateFaceValue(leafIndex, axis, -1, minusValue, minusDistance);
      const bool hasPlus =
        estimateFaceValue(leafIndex, axis, 1, plusValue, plusDistance);
      if (hasMinus && hasPlus && minusDistance + plusDistance > 0.0f) {
        leaf.gradient[axis] =
          (plusValue - minusValue) / (minusDistance + plusDistance);
      } else if (hasPlus && plusDistance > 0.0f) {
        leaf.gradient[axis] = (plusValue - leaf.value) / plusDistance;
      } else if (hasMinus && minusDistance > 0.0f) {
        leaf.gradient[axis] = (leaf.value - minusValue) / minusDistance;
      }
    }
    leaf.gradientReady = true;
  }

  float reconstructLeafValue(std::size_t leafIndex, const glm::vec3& p)
  {
    ensureGradient(leafIndex);
    const LeafSummary& leaf = leaves_[leafIndex];
    const float reconstructed =
      leaf.value + glm::dot(leaf.gradient, p - leaf.center);
    return std::clamp(reconstructed, leaf.limiterMin, leaf.limiterMax);
  }

  void gatherCornerCandidates(const glm::vec3& p,
                              std::vector<std::size_t>& candidates) const
  {
    candidates.clear();
    candidates.reserve(levels_.size() * 8);
    for (std::uint32_t level : levels_) {
      const auto xs = CandidateCoords(p.x, rootMin_.x, rootExtent_.x, level);
      const auto ys = CandidateCoords(p.y, rootMin_.y, rootExtent_.y, level);
      const auto zs = CandidateCoords(p.z, rootMin_.z, rootExtent_.z, level);
      for (std::uint32_t ix : xs) {
        for (std::uint32_t iy : ys) {
          for (std::uint32_t iz : zs) {
            const auto found = leafByCell_.find(CellKey{level, ix, iy, iz});
            if (found == leafByCell_.end()) continue;
            appendUniqueCandidate(candidates, found->second);
          }
        }
      }
    }
  }

  float sampleFromLeafParticles(const ParticleOctree::Node& node,
                                const glm::vec3& p,
                                float fallback) const
  {
    const auto& particles = octree_.getParticles();
    const glm::vec3 extent = glm::max(node.box.max - node.box.min,
                                      glm::vec3(0.0f));
    const float diag2 = glm::dot(extent, extent);
    const float softening2 = std::max(diag2 * 1.0e-4f, 1.0e-30f);
    double weightedValue = 0.0;
    double weightSum = 0.0;
    for (std::size_t i = 0; i < node.count; ++i) {
      const std::size_t ip = node.start + i;
      if (ip >= particles.size()) break;
      const glm::vec3 d = particles[ip].pos - p;
      const float dist2 = glm::dot(d, d) + softening2;
      const double weight = 1.0 / static_cast<double>(dist2);
      weightedValue +=
        weight * static_cast<double>(SafeSigma(particles[ip].val, mapper_));
      weightSum += weight;
    }
    return weightSum > 0.0
      ? static_cast<float>(weightedValue / weightSum)
      : fallback;
  }

  ParticleOctree& octree_;
  const VolumeSigmaMapper& mapper_;
  int reconstructionMode_ = 1;
  glm::vec3 rootMin_{0.0f};
  glm::vec3 rootExtent_{1.0f};
  std::vector<LeafSummary> leaves_;
  std::vector<std::uint32_t> levels_;
  std::unordered_map<CellKey, std::size_t, CellKeyHash> leafByCell_;
  std::unordered_map<CornerKey, float, CornerKeyHash> cache_;
};

struct TreeBuilder {
  ParticleOctree& octree;
  const AdaptiveVolumeTreeBuildParams& params;
  const VolumeSigmaMapper& sigmaMapper;
  SharedCornerSampler cornerSampler;
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

    if (params.cornerReconstructionMode <= 0) {
      out.cornerSigma.fill(out.sigmaAvg);
      return;
    }

    const auto corners = BoxCorners(node.box);
    for (std::size_t i = 0; i < corners.size(); ++i) {
      out.cornerSigma[i] =
        cornerSampler.sample(node, corners[i], out.sigmaAvg);
      out.sigmaMax = std::max(out.sigmaMax, out.cornerSigma[i]);
    }
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

BoundingBox ComputeParticleBounds(const SimulationBlock& block,
                                  const AdaptiveVolumeTreeBuildParams& params)
{
  BoundingBox bounds;
  bounds.min = glm::vec3(std::numeric_limits<float>::max());
  bounds.max = glm::vec3(-std::numeric_limits<float>::max());

  for (const SimulationElement& p : block.particles) {
    const float h = params.expandBoundsByHsml
      ? std::max(renderSupportRadius(p, block.worldToRenderScale), 0.0f)
      : 0.0f;
    const glm::vec3 pos = renderPosition(p, block.worldToRenderScale);
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
  TreeBuilder builder{octree,
                      params,
                      sigmaMapper,
                      SharedCornerSampler{octree,
                                          sigmaMapper,
                                          params.cornerReconstructionMode},
                      {}};
  builder.result.tree.root = builder.appendNode(octree.root());
  builder.result.stats.nodeCount = builder.result.tree.nodes.size();
  builder.result.stats.particleCount = octree.getParticles().size();

  if (!builder.result.tree.valid()) {
    builder.result.warning = "Adaptive volume tree is empty.";
  }

  return builder.result;
}

AdaptiveVolumeTreeBuildResult BuildAdaptiveVolumeTreeFromParticles(
  const SimulationBlock& particles,
  const AdaptiveVolumeTreeBuildParams& params,
  const VolumeSigmaMapper& sigmaMapper)
{
  std::vector<SimulationElementForTree> treeParticles;
  treeParticles.reserve(particles.particles.size());

  for (std::size_t i = 0; i < particles.particles.size(); ++i) {
    const SimulationElement& p = particles.particles[i];
    SimulationElementForTree out;
    out.pos = renderPosition(p, particles.worldToRenderScale);
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
