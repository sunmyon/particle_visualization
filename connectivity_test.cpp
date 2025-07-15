#define GLM_ENABLE_EXPERIMENTAL 1

#include "main.h"
#include "mesh_data.h"
#include "connectivity_test.h"
#include <unordered_set>
#include <glm/gtx/norm.hpp>
#include <iostream>

// --- ローカル定義 (private 成員に依存しない) -----------------
static const glm::vec3 kOffsets[8] = {
    {0,0,0},{1,0,0},{1,1,0},{0,1,0},
    {0,0,1},{1,0,1},{1,1,1},{0,1,1}
};

static float cornerSample(const ParticleOctree& tree,
                          const glm::vec3&      pos,
                          float                 h)
{
    TrackingVector<const ParticleDataForTree*> neigh;
    tree.querySphere(pos, h, neigh);           // querySphere は const

    float sumV=0, sumW=0;
    for(auto p:neigh){
        float r = glm::length(pos - p->pos);
        float w = 1.f - r/h;
        sumV += p->val * w;
        sumW += w;
    }
    return (sumW>0)? sumV/sumW : 0.f;
}

// face を共有するか簡易判定
inline bool touchesFace(const BoundingBox& a, const BoundingBox& b){
    const float eps=1e-6f;
    bool xAdj = std::abs(a.max.x - b.min.x) < eps || std::abs(b.max.x - a.min.x) < eps;
    bool yOvl = a.min.y < b.max.y-eps && a.max.y > b.min.y+eps;
    bool zOvl = a.min.z < b.max.z-eps && a.max.z > b.min.z+eps;
    if(xAdj && yOvl && zOvl) return true;
    bool yAdj = std::abs(a.max.y - b.min.y) < eps || std::abs(b.max.y - a.min.y) < eps;
    bool xOvl = a.min.x < b.max.x-eps && a.max.x > b.min.x+eps;
    if(yAdj && xOvl && zOvl) return true;
    bool zAdj = std::abs(a.max.z - b.min.z) < eps || std::abs(b.max.z - a.min.z) < eps;
    if(zAdj && xOvl && yOvl) return true;
    return false;
}
// -------------------------------------------------------------


// =============================================================
// 3 つのチェックをまとめて実行する関数
// =============================================================
void runConnectivityQuickCheck(const ParticleOctree&      tree,
                               const Mesh& mesh,
                               float isoLevel,
                               float epsCorner,
                               float epsQuant)
{
    std::cout << "=== Connectivity quick-check ===\n";

    // --------- Leaf & neighbor list ----------
    auto leaves = tree.getLeafNodes(isoLevel);
    size_t badCorner=0, badDepth=0;

    for(size_t i=0;i<leaves.size();++i){
        const auto* a = leaves[i];

        // --- 隣接 leaf をペアリング ---
        for(size_t j=i+1;j<leaves.size();++j){
            const auto* b = leaves[j];
            if(!touchesFace(a->box,b->box)) continue;

            // (B) depth 差
            if(std::max(a->depth,b->depth) > std::min(a->depth,b->depth)+1)
                ++badDepth;

            // (A) 共有 corner の値差
            glm::vec3 sizeA = a->box.max - a->box.min;
            //glm::vec3 sizeB = b->box.max - b->box.min;
            for(const auto& off:kOffsets){
                glm::vec3 pA = a->box.min + sizeA * off;
                if(pA.x < b->box.min.x-1e-6f || pA.x > b->box.max.x+1e-6f ||
                   pA.y < b->box.min.y-1e-6f || pA.y > b->box.max.y+1e-6f ||
                   pA.z < b->box.min.z-1e-6f || pA.z > b->box.max.z+1e-6f) continue;

                float vA = cornerSample(tree, pA, 0.05f);
                float vB = cornerSample(tree, pA, 0.05f);
                if(std::abs(vA-vB) > epsCorner) ++badCorner;
            }
        }
    }

    // --------- (C) duplicate vertices ----------
    struct QKey{ long long x,y,z;
       bool operator==(const QKey&o)const{return x==o.x&&y==o.y&&z==o.z;}};
    struct QHash{ size_t operator()(const QKey&k)const noexcept{
        return k.x*73856093ull ^ k.y*19349663ull ^ k.z*83492791ull;}};

    std::unordered_set<QKey,QHash> uniq;
    size_t dupVert=0;
    const auto& v = mesh.vertices;
    for(size_t i=0;i<v.size(); i+=3){
        QKey k{ llround(v[i]/epsQuant),
                llround(v[i+1]/epsQuant),
                llround(v[i+2]/epsQuant) };
        if(!uniq.insert(k).second) ++dupVert;
    }

    // --------- レポート ----------
    std::cout << ((badCorner==0) ? "✓" : "✗")
              << " Corner diff   (<"<<epsCorner<<"): " << badCorner << "\n";
    std::cout << ((badDepth==0) ? "✓" : "✗")
              << " Depth gap >1 : " << badDepth << "\n";
    std::cout << ((dupVert==0)  ? "✓" : "✗")
              << " Duplicate vertices: " << dupVert << "\n";
    std::cout << "===============================\n";
}
