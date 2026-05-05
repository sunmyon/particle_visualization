#include "render/particle_lod.h"

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>

#include <glm/geometric.hpp>

#include "render/render_resources.h"

namespace {

constexpr std::uint32_t kMaxParticleLodTypes = 6;

glm::vec3 ParticlePosition(const RenderParticle& p)
{
  return glm::vec3(p.pos[0], p.pos[1], p.pos[2]);
}

glm::vec3 BoundsCenter(const ParticleLodBounds& bounds)
{
  return 0.5f * (bounds.min + bounds.max);
}

float BoundsRadius(const ParticleLodBounds& bounds)
{
  return 0.5f * glm::length(bounds.max - bounds.min);
}

ParticleLodBounds ComputeBounds(const std::vector<RenderParticle>& particles)
{
  ParticleLodBounds bounds;
  bounds.min = glm::vec3(std::numeric_limits<float>::max());
  bounds.max = glm::vec3(-std::numeric_limits<float>::max());

  for (const RenderParticle& p : particles) {
    const glm::vec3 pos = ParticlePosition(p);
    bounds.min = glm::min(bounds.min, pos);
    bounds.max = glm::max(bounds.max, pos);
  }

  const glm::vec3 extent = bounds.max - bounds.min;
  const float maxExtent = std::max({extent.x, extent.y, extent.z});
  const float pad = std::max(1.0e-6f, maxExtent * 1.0e-5f);
  bounds.min -= glm::vec3(pad);
  bounds.max += glm::vec3(pad);
  return bounds;
}

int ChildIndexForPoint(const glm::vec3& pos, const glm::vec3& center)
{
  return (pos.x >= center.x ? 1 : 0) |
         (pos.y >= center.y ? 2 : 0) |
         (pos.z >= center.z ? 4 : 0);
}

ParticleLodBounds ChildBounds(const ParticleLodBounds& bounds, int child)
{
  const glm::vec3 center = BoundsCenter(bounds);
  ParticleLodBounds out;
  out.min = glm::vec3((child & 1) ? center.x : bounds.min.x,
                      (child & 2) ? center.y : bounds.min.y,
                      (child & 4) ? center.z : bounds.min.z);
  out.max = glm::vec3((child & 1) ? bounds.max.x : center.x,
                      (child & 2) ? bounds.max.y : center.y,
                      (child & 4) ? bounds.max.z : center.z);
  return out;
}

void FillRepresentative(const std::vector<RenderParticle>& particles,
                        const std::vector<std::uint32_t>& indices,
                        ParticleLodNode& node)
{
  glm::vec3 posSum(0.0f);
  float hsmlSum = 0.0f;
  float valueSum = 0.0f;
  std::array<std::uint32_t, kMaxParticleLodTypes> typeCounts{};
  std::uint8_t flags = 0;

  for (std::uint32_t i = 0; i < node.count; ++i) {
    const RenderParticle& p = particles[indices[node.start + i]];
    posSum += ParticlePosition(p);
    hsmlSum += p.hsml;
    valueSum += p.val_show;
    flags = static_cast<std::uint8_t>(flags | p.flag_stress);
    if (p.type < typeCounts.size()) {
      ++typeCounts[p.type];
    }
  }

  const float invCount = node.count > 0
                       ? 1.0f / static_cast<float>(node.count)
                       : 0.0f;
  const glm::vec3 pos = posSum * invCount;
  node.representativePos[0] = pos.x;
  node.representativePos[1] = pos.y;
  node.representativePos[2] = pos.z;
  node.representativeHsml = hsmlSum * invCount;
  node.representativeValue = valueSum * invCount;
  node.representativeFlagStress = flags;

  auto bestType = std::max_element(typeCounts.begin(), typeCounts.end());
  node.representativeType =
    static_cast<std::uint8_t>(std::distance(typeCounts.begin(), bestType));
}

std::int32_t BuildNodeRecursive(const std::vector<RenderParticle>& particles,
                                ParticleLodTree& tree,
                                const ParticleLodBounds& bounds,
                                std::uint32_t start,
                                std::uint32_t count,
                                std::uint32_t depth,
                                const ParticleLodSettings& settings)
{
  const std::int32_t nodeIndex = static_cast<std::int32_t>(tree.nodes.size());
  ParticleLodNode node;
  node.bounds = bounds;
  node.start = start;
  node.count = count;
  node.depth = depth;
  node.radius = BoundsRadius(bounds);
  tree.nodes.push_back(node);

  ParticleLodNode& stored = tree.nodes.back();
  FillRepresentative(particles, tree.indices, stored);

  if (count <= settings.minNodeParticles || depth >= settings.maxDepth) {
    return nodeIndex;
  }

  const glm::vec3 center = BoundsCenter(bounds);
  std::uint32_t childStart = start;
  for (int child = 0; child < 8; ++child) {
    auto begin = tree.indices.begin() + childStart;
    auto end = tree.indices.begin() + start + count;
    auto split = std::partition(begin, end, [&](std::uint32_t particleIndex) {
      return ChildIndexForPoint(ParticlePosition(particles[particleIndex]),
                                center) == child;
    });

    const std::uint32_t childCount =
      static_cast<std::uint32_t>(std::distance(begin, split));
    if (childCount > 0) {
      tree.nodes[static_cast<std::size_t>(nodeIndex)].children[child] =
        BuildNodeRecursive(particles,
                           tree,
                           ChildBounds(bounds, child),
                           childStart,
                           childCount,
                           depth + 1,
                           settings);
    }
    childStart += childCount;
  }

  return nodeIndex;
}

bool HasChildren(const ParticleLodNode& node)
{
  for (int child : node.children) {
    if (child >= 0) {
      return true;
    }
  }
  return false;
}

RenderParticle MakeRepresentativeParticle(const ParticleLodNode& node)
{
  RenderParticle p;
  p.pos[0] = node.representativePos[0];
  p.pos[1] = node.representativePos[1];
  p.pos[2] = node.representativePos[2];
  p.type = node.representativeType;
  p.flag_stress = node.representativeFlagStress;
  p.hsml = node.representativeHsml;
  p.val_show = node.representativeValue;
  return p;
}

void AppendLeafParticles(const std::vector<RenderParticle>& particles,
                         const ParticleLodTree& tree,
                         const ParticleLodNode& node,
                         std::vector<RenderParticle>& out)
{
  for (std::uint32_t i = 0; i < node.count; ++i) {
    out.push_back(particles[tree.indices[node.start + i]]);
  }
}

bool WouldExceedOutputLimit(const std::vector<RenderParticle>& out,
                            std::uint32_t extra,
                            std::size_t maxOutputParticles)
{
  return out.size() + static_cast<std::size_t>(extra) > maxOutputParticles;
}

bool AppendDistanceLodNode(const std::vector<RenderParticle>& particles,
                           const ParticleLodTree& tree,
                           std::int32_t nodeIndex,
                           const glm::vec3& focus,
                           const ParticleLodSettings& settings,
                           std::size_t maxOutputParticles,
                           std::vector<RenderParticle>& out)
{
  if (nodeIndex < 0 ||
      static_cast<std::size_t>(nodeIndex) >= tree.nodes.size()) {
    return true;
  }

  const ParticleLodNode& node = tree.nodes[static_cast<std::size_t>(nodeIndex)];
  const float distance = std::max(1.0e-6f,
                                  glm::length(BoundsCenter(node.bounds) - focus));
  const bool smallFromFocus = (node.radius / distance) < settings.theta;

  if (smallFromFocus) {
    if (WouldExceedOutputLimit(out, 1, maxOutputParticles)) {
      return false;
    }
    out.push_back(MakeRepresentativeParticle(node));
    return true;
  }

  if (!HasChildren(node)) {
    if (WouldExceedOutputLimit(out, node.count, maxOutputParticles)) {
      return false;
    }
    AppendLeafParticles(particles, tree, node, out);
    return true;
  }

  for (int childIndex : node.children) {
    if (!AppendDistanceLodNode(particles,
                               tree,
                               childIndex,
                               focus,
                               settings,
                               maxOutputParticles,
                               out)) {
      return false;
    }
  }
  return true;
}

std::uint32_t ChildMask(const ParticleLodNode& node)
{
  std::uint32_t mask = 0;
  for (int child = 0; child < 8; ++child) {
    if (node.children[child] >= 0) {
      mask |= (1u << static_cast<std::uint32_t>(child));
    }
  }
  return mask;
}

} // namespace

void BuildParticleLodTree(const std::vector<RenderParticle>& particles,
                          const ParticleLodSettings& settings,
                          ParticleLodTree& out)
{
  out = ParticleLodTree{};
  if (particles.empty()) {
    return;
  }

  out.indices.resize(particles.size());
  std::iota(out.indices.begin(), out.indices.end(), 0u);
  out.nodes.reserve(particles.size() / std::max<std::uint32_t>(1u, settings.minNodeParticles));

  BuildNodeRecursive(particles,
                     out,
                     ComputeBounds(particles),
                     0,
                     static_cast<std::uint32_t>(particles.size()),
                     0,
                     settings);
  out.valid = !out.nodes.empty();
}

void BuildParticleLodGpuTree(const ParticleLodTree& tree,
                             ParticleLodGpuTree& out)
{
  out = ParticleLodGpuTree{};
  if (!tree.valid || tree.nodes.empty()) {
    return;
  }

  const std::size_t nodeCount = tree.nodes.size();
  out.nodeCenterRadius.resize(nodeCount);
  out.representativePosHsml.resize(nodeCount);
  out.representativeValue.resize(nodeCount);
  out.nodeMeta.resize(nodeCount);
  out.childA.resize(nodeCount);
  out.childB.resize(nodeCount);
  out.representativeMeta.resize(nodeCount);
  out.indices = tree.indices;

  std::vector<std::uint32_t> parentIndex(
    nodeCount, std::numeric_limits<std::uint32_t>::max());
  for (std::size_t i = 0; i < nodeCount; ++i) {
    const ParticleLodNode& node = tree.nodes[i];
    for (int child : node.children) {
      if (child >= 0 && static_cast<std::size_t>(child) < nodeCount) {
        parentIndex[static_cast<std::size_t>(child)] =
          static_cast<std::uint32_t>(i);
      }
    }
  }

  for (std::size_t i = 0; i < nodeCount; ++i) {
    const ParticleLodNode& node = tree.nodes[i];
    const glm::vec3 center = BoundsCenter(node.bounds);
    out.nodeCenterRadius[i] =
      glm::vec4(center.x, center.y, center.z, node.radius);
    out.representativePosHsml[i] =
      glm::vec4(node.representativePos[0],
                node.representativePos[1],
                node.representativePos[2],
                node.representativeHsml);
    out.representativeValue[i] =
      glm::vec4(node.representativeValue, 0.0f, 0.0f, 0.0f);
    out.nodeMeta[i] =
      glm::uvec4(node.start, node.count, node.depth, ChildMask(node));
    out.childA[i] =
      glm::ivec4(node.children[0],
                 node.children[1],
                 node.children[2],
                 node.children[3]);
    out.childB[i] =
      glm::ivec4(node.children[4],
                 node.children[5],
                 node.children[6],
                 node.children[7]);
    out.representativeMeta[i] =
      glm::uvec4(node.representativeType,
                 node.representativeFlagStress,
                 parentIndex[i],
                 0u);
    if (ChildMask(node) == 0u) {
      out.maxLeafCount = std::max(out.maxLeafCount, node.count);
    }
  }

  out.valid = true;
}

void BuildParticleLodOrderedParticles(
  const std::vector<RenderParticle>& particles,
  const ParticleLodTree& tree,
  std::vector<RenderParticle>& out)
{
  out.clear();
  if (!tree.valid || tree.indices.empty()) {
    return;
  }

  out.reserve(tree.indices.size());
  for (std::uint32_t index : tree.indices) {
    if (index < particles.size()) {
      out.push_back(particles[index]);
    }
  }
}

bool BuildParticleLodProxyDrawList(const std::vector<RenderParticle>& particles,
                                   const ParticleLodTree& tree,
                                   const glm::vec3& focus,
                                   const ParticleLodSettings& settings,
                                   std::vector<RenderParticle>& out)
{
  out.clear();
  if (!tree.valid || tree.nodes.empty()) {
    return false;
  }

  const std::size_t targetCount =
    std::max<std::size_t>(1,
      static_cast<std::size_t>(static_cast<double>(tree.indices.size()) *
                               static_cast<double>(settings.proxyFraction)));

  out.reserve(targetCount);
  const bool ok = AppendDistanceLodNode(particles,
                                        tree,
                                        0,
                                        focus,
                                        settings,
                                        targetCount,
                                        out);
  if (!ok) {
    out.clear();
  }
  return ok;
}

std::size_t EstimateParticleLodTreeBytes(const ParticleLodTree& tree)
{
  return tree.indices.size() * sizeof(std::uint32_t) +
         tree.nodes.size() * sizeof(ParticleLodNode);
}

std::size_t EstimateParticleLodGpuTreeBytes(const ParticleLodGpuTree& tree)
{
  return tree.nodeCenterRadius.size() * sizeof(glm::vec4) +
         tree.representativePosHsml.size() * sizeof(glm::vec4) +
         tree.representativeValue.size() * sizeof(glm::vec4) +
         tree.nodeMeta.size() * sizeof(glm::uvec4) +
         tree.childA.size() * sizeof(glm::ivec4) +
         tree.childB.size() * sizeof(glm::ivec4) +
         tree.representativeMeta.size() * sizeof(glm::uvec4) +
         tree.indices.size() * sizeof(std::uint32_t) +
         sizeof(tree.maxLeafCount);
}
