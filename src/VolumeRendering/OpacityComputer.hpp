// OpacityComputer.hpp
#pragma once
#include <functional>
#include <cassert>
#include "BVH/BVH.hpp"
#include "OctTree/ParticleOctree.h"

#include <unordered_map>

struct NodeInfo {
  int     index      = -1;     // preorder 連番
  float   sigmaAvg   = 0.f;    // ノード平均 σ
  float   sigmaMax   = 0.f;    // ノード最大 σ
  size_t  start      = 0;      // 葉: 粒子開始
  size_t  count      = 0;      // 葉: 粒子数
  int     child[8] = { -1,-1,-1,-1,-1,-1,-1,-1 }; // 直下8子の index を明示
};

// 体積（AABB）
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
    // 葉：粒子から sigma を集計
    float sMax = 0.f, sSum = 0.f;
    const auto& part = tree.getParticles();
    for (size_t i = 0; i < n->count; ++i) {
      size_t pi = n->start + i;
      float rho   = part[pi].val;               // あなたの値に合わせる
      float sigma = rho2sigma(rho);
      //float sigma = (rho > 0.01f ? 1.f : 0.f);   // 仮
      sSum += sigma;
      sMax  = std::max(sMax, sigma);
    }
    info[my].sigmaAvg = (n->count > 0) ? (sSum / float(n->count)) : 0.f;
    info[my].sigmaMax = sMax;
    info[my].start    = n->start;
    info[my].count    = n->count;
    // child[] は既に -1 で初期化済み
    return my;
  }

  // 内部：子を再帰して「その index を child[] に直書き」＋ σ を集約
  float volSum = 0.f, weighted = 0.f, sMax = 0.f;
  for (int c = 0; c < 8; ++c) {
    const auto& up = n->children[c];
    if (!up) { info[my].child[c] = -1; continue; }

    int ci = preorderBuild(tree, up.get(), rho2sigma, order, info, toIdx);
    info[my].child[c] = ci;

    float v = volumeOf(order[ci]->box);           // AABB 体積
    volSum   += v;
    weighted += info[ci].sigmaAvg * v;
    sMax      = std::max(sMax, info[ci].sigmaMax);
  }
  info[my].sigmaAvg = (volSum > 0.f) ? (weighted / volSum) : 0.f;
  info[my].sigmaMax = sMax;
  info[my].start = info[my].count = 0;              // 内部は 0

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
  tree.evaluateEdgeValueForAllLeaves();
}


namespace lbvh {
/// sigmaOfRho:  rho -> sigma を返す関数（TFのLUTや式）
/// 役割: BuildResult の gpu[i].sigma0 と nodes[*].sigma を更新する
  inline void computeSigma(BuildResult& br, const RhoSigmaLUT& rho2sigma)
  {
    const int N = static_cast<int>(br.gpu.size());
    if (N == 0) return;

    // 期待形状を軽く検証（2N-1）
    if (static_cast<int>(br.nodes.size()) != 2*N - 1) {
      // 形が想定と違う場合は何もしない（または assert に置換）
      assert(static_cast<int>(br.nodes.size()) == 2*N - 1);
      return;
    }

    const int leafBase = br.leafBase; // 通常 N-1

    // 1) 葉（粒子）σを更新
    for (int i = 0; i < N; ++i) {
      const float rho = br.gpu[i].rho;
      //const float sig = rho2sigma(rho);
      const float sig = (rho > 0.01?1.:0.);
      
      br.gpu[i].sigma0 = sig;                 // 粒子側にも保持したい場合
      br.nodes[leafBase + i].sigma_avg = sig;     // 対応する葉ノードの σ
      br.nodes[leafBase + i].sigma_max = sig;     // 対応する葉ノードの σ
    }

    // 2) 内部ノード σ を下から集約（left/right の σ の和）
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

/// 既に gpu[*].sigma0（葉）が埋まっている前提で、
/// nodes[*].sigma だけを再伝播したい時用（高速）
  inline void propagateSigmaFromLeaves(BuildResult& br)
  {
    const int N = static_cast<int>(br.gpu.size());
    if (N == 0) return;
    if (static_cast<int>(br.nodes.size()) != 2*N - 1) return;

    const int leafBase = br.leafBase;

    // 葉ノードへ gpu の σ を反映
    for (int i = 0; i < N; ++i) {
      br.nodes[leafBase + i].sigma_max = br.gpu[i].sigma0;
      br.nodes[leafBase + i].sigma_avg = br.gpu[i].sigma0;
    }
    // 内部を集約
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
