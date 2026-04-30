#include "render/particle_lod.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

#include <glm/geometric.hpp>

#include "render/frame_matrices.h"
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

void AppendLodNode(const ParticleLodTree& tree,
                   std::int32_t nodeIndex,
                   const FrameMatrices& frame,
                   const ParticleLodSettings& settings,
                   std::vector<RenderParticle>& out)
{
  if (nodeIndex < 0 ||
      static_cast<std::size_t>(nodeIndex) >= tree.nodes.size()) {
    return;
  }

  const ParticleLodNode& node = tree.nodes[static_cast<std::size_t>(nodeIndex)];
  const glm::vec4 viewCenter =
    frame.view * glm::vec4(BoundsCenter(node.bounds), 1.0f);
  const float distance = std::max(1.0e-6f, std::abs(viewCenter.z));
  const float screenRadius = frame.focalPx * node.radius / distance;

  if (!HasChildren(node) || screenRadius <= settings.pixelThreshold) {
    out.push_back(MakeRepresentativeParticle(node));
    return;
  }

  for (int child : node.children) {
    AppendLodNode(tree, child, frame, settings, out);
  }
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

void BuildParticleLodDrawList(const ParticleLodTree& tree,
                              const FrameMatrices& frame,
                              const ParticleLodSettings& settings,
                              std::vector<RenderParticle>& out)
{
  out.clear();
  if (!tree.valid || tree.nodes.empty()) {
    return;
  }

  AppendLodNode(tree, 0, frame, settings, out);
}

std::size_t EstimateParticleLodTreeBytes(const ParticleLodTree& tree)
{
  return tree.indices.size() * sizeof(std::uint32_t) +
         tree.nodes.size() * sizeof(ParticleLodNode);
}
