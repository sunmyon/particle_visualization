// OpacityComputer.hpp
#pragma once
#include <functional>
#include <cassert>
#include "BVH/BVH.hpp"
#include "data/spatial/particle_octree.h"

#include <unordered_map>

struct NodeInfo {
  int     index      = -1;     // Preorder serial index.
  float   sigmaAvg   = 0.f;    // Node average sigma.
  float   sigmaMax   = 0.f;    // Node maximum sigma.
  size_t  start      = 0;      // Leaf: first particle.
  size_t  count      = 0;      // Leaf: particle count.
  int     child[8] = { -1,-1,-1,-1,-1,-1,-1,-1 }; // Explicit indices of the 8 direct children.
};

// AABB volume.
static inline float volumeOf(const BoundingBox& b){
  glm::vec3 d = glm::max(b.max - b.min, glm::vec3(0.0f));
  return d.x * d.y * d.z;
}

static int preorderBuild(ParticleOctree& tree,
                         const ParticleOctree::Node* n,
                         const RhoSigmaLUT& rho2sigma,
                         std::vector<const ParticleOctree::Node*>& order,
                         std::vector<NodeInfo>& info,
                         std::unordered_map<const ParticleOctree::Node*, int>& toIdx)
{
  const int my = (int)order.size();
  order.push_back(n);
  info.emplace_back();
  info.back().index = my;
  toIdx[n] = my;

  if (n->isLeaf) {
    // Leaf: accumulate sigma from particles.
    float sMax = 0.f, sSum = 0.f;
    const auto& part = tree.getParticles();
    for (size_t i = 0; i < n->count; ++i) {
      size_t pi = n->start + i;
      float rho   = part[pi].val;               // Adjust to the desired particle value.
      float sigma = rho2sigma(rho);
      //float sigma = (rho > 0.01f ? 1.f : 0.f);   // Temporary alternative.
      sSum += sigma;
      sMax  = std::max(sMax, sigma);
    }
    info[my].sigmaAvg = (n->count > 0) ? (sSum / float(n->count)) : 0.f;
    info[my].sigmaMax = sMax;
    info[my].start    = n->start;
    info[my].count    = n->count;
    // child[] is already initialized to -1.
    return my;
  }

  // Internal node: recurse into children, write their indices to child[], and aggregate sigma.
  float volSum = 0.f, weighted = 0.f, sMax = 0.f;
  for (int c = 0; c < 8; ++c) {
    const auto& up = n->children[c];
    if (!up) { info[my].child[c] = -1; continue; }

    int ci = preorderBuild(tree, up.get(), rho2sigma, order, info, toIdx);
    info[my].child[c] = ci;

    float v = volumeOf(order[ci]->box);           // AABB volume.
    volSum   += v;
    weighted += info[ci].sigmaAvg * v;
    sMax      = std::max(sMax, info[ci].sigmaMax);
  }
  info[my].sigmaAvg = (volSum > 0.f) ? (weighted / volSum) : 0.f;
  info[my].sigmaMax = sMax;
  info[my].start = info[my].count = 0;              // Internal nodes use 0.

  return my;
}


inline void buildIndexAndSigma(ParticleOctree& tree,
			       const RhoSigmaLUT& rho2sigma,
			       std::vector<const ParticleOctree::Node*>& order,
			       std::vector<NodeInfo>& info,
			       std::unordered_map<const ParticleOctree::Node*, int>& toIdx)
{
  order.clear();
  info.clear();
  toIdx.clear();
  order.reserve(1024);
  info.reserve(1024);
  
  preorderBuild(tree, &tree.root(), rho2sigma, order, info, toIdx);
}


namespace lbvh {
/// sigmaOfRho: function returning sigma from rho, using a TF LUT or formula.
/// Updates gpu[i].sigma0 and nodes[*].sigma in BuildResult.
  inline void computeSigma(BuildResult& br, const RhoSigmaLUT& rho2sigma)
  {
    const int N = static_cast<int>(br.gpu.size());
    if (N == 0) return;

    // Lightly validate the expected 2N-1 shape.
    if (static_cast<int>(br.nodes.size()) != 2*N - 1) {
      // Do nothing if the shape is unexpected, or replace this with an assert.
      assert(static_cast<int>(br.nodes.size()) == 2*N - 1);
      return;
    }

    const int leafBase = br.leafBase; // Usually N - 1.

    // 1) Update sigma on leaves, one per particle.
    for (int i = 0; i < N; ++i) {
      const float rho = br.gpu[i].rho;
      //const float sig = rho2sigma(rho);
      const float sig = (rho > 0.01?1.:0.);
      
      br.gpu[i].sigma0 = sig;                     // Keep it on the particle side if desired.
      br.nodes[leafBase + i].sigma_avg = sig;     // Sigma of the corresponding leaf node.
      br.nodes[leafBase + i].sigma_max = sig;     // Sigma of the corresponding leaf node.
    }

    // 2) Aggregate internal-node sigma bottom-up from left and right children.
    for (int n = leafBase - 1; n >= 0; --n) {
      const int L = br.nodes[n].left;
      const int R = br.nodes[n].right;

      const float vL = br.nodes[L].volume;
      const float vR = br.nodes[R].volume;
      
      const float sL = (L >= 0) ? br.nodes[L].sigma_avg : 0.0f;
      const float sR = (R >= 0) ? br.nodes[R].sigma_avg : 0.0f;

      const float sL_max = (L >= 0) ? br.nodes[L].sigma_max : 0.0f;
      const float sR_max = (R >= 0) ? br.nodes[R].sigma_max : 0.0f;

      br.nodes[n].sigma_max = std::max(sL_max, sR_max);
      br.nodes[n].sigma_avg = (sL*vL + sR*vR) / (vL + vR);
    }
  }

/// Fast path for re-propagating only nodes[*].sigma when gpu[*].sigma0 is already filled.
  inline void propagateSigmaFromLeaves(BuildResult& br)
  {
    const int N = static_cast<int>(br.gpu.size());
    if (N == 0) return;
    if (static_cast<int>(br.nodes.size()) != 2*N - 1) return;

    const int leafBase = br.leafBase;

    // Reflect GPU sigma values into leaf nodes.
    for (int i = 0; i < N; ++i) {
      br.nodes[leafBase + i].sigma_max = br.gpu[i].sigma0;
      br.nodes[leafBase + i].sigma_avg = br.gpu[i].sigma0;
    }
    // Aggregate internal nodes.
    for (int n = leafBase - 1; n >= 0; --n) {
      const int L = br.nodes[n].left;
      const int R = br.nodes[n].right;

      const float vL = br.nodes[L].volume;
      const float vR = br.nodes[R].volume;
      
      const float sL = (L >= 0) ? br.nodes[L].sigma_avg : 0.0f;
      const float sR = (R >= 0) ? br.nodes[R].sigma_avg : 0.0f;

      const float sL_max = (L >= 0) ? br.nodes[L].sigma_max : 0.0f;
      const float sR_max = (R >= 0) ? br.nodes[R].sigma_max : 0.0f;

      br.nodes[n].sigma_max = std::max(sL_max, sR_max);
      br.nodes[n].sigma_avg = (sL*vL + sR*vR) / (vL + vR);
    }
  }

} // namespace lbvh
