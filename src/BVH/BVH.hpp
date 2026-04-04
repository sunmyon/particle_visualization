#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include <numeric>
#include <limits>
#include <cmath>

#include "core/tracking_vector.h"
#include "data/particle_data.h"

namespace lbvh {
  // ----------------- 基本ユーティリティ -----------------
  struct Vec3 { float x=0,y=0,z=0; Vec3()=default; Vec3(float X,float Y,float Z):x(X),y(Y),z(Z){} };
  inline Vec3 operator-(const Vec3&a,const Vec3&b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
  inline Vec3 minV(const Vec3&a,const Vec3&b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};}
  inline Vec3 maxV(const Vec3&a,const Vec3&b){return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}

  struct AABB {
    Vec3 bmin{  std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity() };
    Vec3 bmax{ -std::numeric_limits<float>::infinity(),
               -std::numeric_limits<float>::infinity(),
               -std::numeric_limits<float>::infinity() };
    void expand(const Vec3&p){ bmin=minV(bmin,p); bmax=maxV(bmax,p); }
    void expand(const AABB&o){ bmin=minV(bmin,o.bmin); bmax=maxV(bmax,o.bmax); }
  };

  // 出力：GPU転送用（SSBOにそのまま置ける）
  struct GpuParticle {
    float pos[4];
    float rho;
    float sigma0;    // 強度    
    uint32_t id;     // 元ID（任意だが便利）
  };

  // BVHノード（AoS最小）
  struct BVHNodeCPU {
    float bmin[4];   // xyz + pad
    float bmax[4];
    float sigma_max;
    float sigma_avg;
    float volume;
    int   left;      // 内部: 子 index
    int   right;
    int   first;     // 葉: 粒子範囲の開始（本設計は1粒子=1leafだが将来拡張用）
    int   count;     // 葉: 個数（現状 1）
  };

  struct BuildResult {
    std::vector<BVHNodeCPU> nodes;   // 2*N-1
    std::vector<GpuParticle> gpu;    // N
    int root = -1;                   // ふつう 0
    int leafBase = 0;                // N-1 （シェーダに渡す用）
    int nodeCount = 0;               // 2*N-1
  };
  
  // ----------------- Builder IF（拡張ポイント） -----------------
  struct IBVHBuilder {
    virtual ~IBVHBuilder() = default;
    virtual BuildResult build(const TrackingVector<ParticleData>& in) = 0;
  };

  // ======================================================
  //                  Morton(LBVH) Builder
  // ======================================================
  class MortonBuilder final : public IBVHBuilder {
  public:
    explicit MortonBuilder(int mortonBits=21) : bits_(mortonBits) {}

    BuildResult build(const TrackingVector<ParticleData>& in) override
    {
      int N = (int)in.size();

      std::vector<BVHNodeCPU> outNodes;
      std::vector<GpuParticle> outGpu;
      
      outNodes.clear();
      outGpu.clear();

      if (N==0)
	return BuildResult{};

      // 1) world AABB
      AABB world;
      for (auto& p: in) {
	float r = p.Hsml;
	world.expand({p.pos[0] - r, p.pos[1] - r, p.pos[2] - r});
	world.expand({p.pos[0] + r, p.pos[1] + r, p.pos[2] + r});
      }
      wmin_ = world.bmin;
      wmax_ = world.bmax;

      // 2) Morton code
      codes_.resize(N);
      computeMortonCodes(in);

      // 3) ソート（コード昇順）→ 並べ替え index
      std::vector<int> idx(N);
      std::iota(idx.begin(), idx.end(), 0);
      std::sort(idx.begin(), idx.end(), [&](int a, int b){
	if (codes_[a] != codes_[b]) return codes_[a] < codes_[b];
	return a < b; // 同値は元インデックスでタイブレーク
      });

      codesSorted_.resize(N);
      int count = 0;
      for (int i=0;i<N;i++){
	codesSorted_[i] = codes_[ idx[i] ];
	if(i > 0){
	  if(codesSorted_[i] == codesSorted_[i-1]){
	    printf("i=%d %lld overlap!! combine or discard\n", i, codesSorted_[i]);	    
	    continue;
	  }
	}

	if(count != i){
	  idx[count] = idx[i];
	  codesSorted_[count] = codes_[idx[i]];
	}
	
	count++;
      }

      N = count;
      codesSorted_.resize(N);
      
      // 4) 並べ替え済み GPU 粒子を作成（AoS）
      outGpu.resize(N);
      for (int i=0;i<N;i++){
	const auto& p = in[idx[i]];
	GpuParticle gp{};
	gp.pos[0] = p.pos[0];
	gp.pos[1] = p.pos[1];
	gp.pos[2] = p.pos[2];
	gp.pos[3] = p.Hsml;

	gp.rho    = p.density;
	gp.sigma0 = 1.;
	gp.id     = p.ID;
      
	outGpu[i] = gp;
      }

      // 5) ノード配列（内部 N-1、葉 N → 合計 2N-1）
      outNodes.assign(2*N-1, BVHNodeCPU{});
      auto leafIndex = [&](int i){ return (N-1 + i); };

      // 葉：1粒子=1leaf
      for (int i=0;i<N;i++){
	float r = outGpu[i].pos[3];
	BVHNodeCPU& L = outNodes[leafIndex(i)];
	for(int k=0;k<3;k++){
	  L.bmin[k] = outGpu[i].pos[k]-r;
	  L.bmax[k] = outGpu[i].pos[k]+r;
	}
	L.bmin[3] = L.bmax[3] = 0;
	L.volume = (4./3.) * 3.1415926535f * r*r*r;
	
	L.left = L.right = -1;
	L.first = i; L.count = 1;
      }

      std::vector<int> parent(N-1, -1);
      
      // 6) 内部ノードリンク（Karras 2012）
      for (int i=0;i<N-1;i++){
	int dL = lcp(i, i-1);
	int dR = lcp(i, i+1);
	int dir = (dR - dL) > 0 ? +1 : -1;
	int dMin = std::min(dL, dR);
	int j = findRangeEnd(i, dir, dMin);

	// split も [first,last] の中で求める
	int split = findSplit(i, j);
	
	int leftId, rightId;
	if (dir > 0) {
	  // 範囲 [i..j] を split で分割 → 左は [i..split], 右は [split+1..j]
	  leftId  = (split == i)     ? (N - 1 + i)     : split;
	  rightId = (split + 1 == j) ? (N - 1 + j)     : (split + 1);
	} else {
	  // 範囲 [j..i] を split で分割 → 左は [j..split], 右は [split+1..i]
	  leftId  = (split == j)     ? (N - 1 + j)     : split;
	  rightId = (split + 1 == i) ? (N - 1 + i)     : (split + 1);
	}

#ifdef DEBUG_LBVH
	//printf("parent=%d dL=%d dR=%d j=%d\n", i, dL, dR, j);

	int first = std::min(i, j);
	int last = std::max(i, j);

	if(first < 0 || first > split || split >= last || last >= N)
	  printf("[BUG] line=%d\n", __LINE__);	  
		
	if(lcp(i,first) <= dMin || lcp(i,last) <= dMin)
	  printf("[BUG] line=%d lcp(i,first) or lcp(i, last) is too large.\n"
		 , __LINE__, lcp(i,first), lcp(i,last) );

	if(dir > 0){
	  if(j != last)
	    printf("[BUG] line=%d\n", __LINE__);

	  if(j != N - 1 && lcp(i,j+1) > dMin)
	    printf("[BUG] line=%d\n", __LINE__);
	}else{
	  if(j != first)
	    printf("[BUG] line=%d\n", __LINE__);

	  if(j != 0 && lcp(i,j-1) > dMin)
	    printf("[BUG] line=%d\n", __LINE__);
	}

	int common = lcp(i, j);
	bool flag = false;
	if(dir > 0){
	  if(lcp(i, split) <= common){
	    printf("[BUG] line=%d common=%d lcp(i,split)=%d\n", __LINE__, common, lcp(i, split));
	    flag = true;
	  }
	
	  if(lcp(i, split+1) > common){
	    printf("[BUG] line=%d common=%d lcp(i,split+1)=%d\n", __LINE__, common, lcp(i, split+1));
	    flag = true;
	  }
	}else{
	  if(lcp(i, split) > common){
	    printf("[BUG] line=%d common=%d lcp(i,split)=%d\n", __LINE__, common, lcp(i, split));
	    flag = true;
	  }
	
	  if(lcp(i, split+1) <= common){
	    printf("[BUG] line=%d common=%d lcp(i,split+1)=%d\n", __LINE__, common, lcp(i, split+1));
	    flag = true;
	  }
	}

	if(flag){
	  for(int n=first;n<last;n++){
	    printf("[CHECK] n=%d lcp=%d common=%d\n", n, lcp(i,n), common);
	  }
	}
	
	if (leftId == i || rightId == i) {
	  printf("[BUG] self-child detected: i=%d split=%d left=%d right=%d\n",
		 i, split, leftId, rightId);
	}
#endif
	
	BVHNodeCPU& I = outNodes[i];
	I.left  = leftId;
	I.right = rightId;
	I.first = 0;
	I.count = 0;
	
	if (leftId  < N-1) parent[leftId ] = i;
	if (rightId < N-1) parent[rightId] = i;
      }

      int root = 0, count1 = 0;
      for (int k=0; k<N-1; ++k){
	if (parent[k] == -1) {
	  root = k;	  
	  count1++;
	}
      }
      
      printf("root=%d count=%d\n", root, count1);
      root_ = root;
      
      // 7) AABB refit（下から）— ルート=0
      refit(outNodes, root, N);

      BuildResult r;

      r.nodes = std::move(outNodes);
      r.gpu   = std::move(outGpu);
      r.root  = root_;
      r.leafBase  = N-1;
      r.nodeCount = (int)r.nodes.size();
      
      return r;
    }

  private:
    // --- Morton ---
    static uint32_t q01(float x, int bits){
      const float eps = std::ldexp(1.0f, -bits);
      if (x < 0.0f) x = 0.0f;
      if (x >= 1.0f) x = 1.0f - eps;
      return (uint32_t)std::floor(x * float(1u<<bits));
    }
    
    static uint64_t expand21(uint32_t v){
      uint64_t x = v & 0x1fffffULL;
      x = (x | (x << 32)) & 0x1f00000000ffffULL;
      x = (x | (x << 16)) & 0x1f0000ff0000ffULL;
      x = (x | (x << 8 )) & 0x100f00f00f00f00fULL;
      x = (x | (x << 4 )) & 0x10c30c30c30c30c3ULL;
      x = (x | (x << 2 )) & 0x1249249249249249ULL;
      return x;
    }
    
    static uint64_t morton3D(uint32_t qx,uint32_t qy,uint32_t qz){
      return (expand21(qx) << 0) | (expand21(qy) << 1) | (expand21(qz) << 2);
    }

#if defined(_MSC_VER)
#include <intrin.h>
#endif
    static inline int clz32(uint32_t x) {
#if defined(_MSC_VER)
  	if (!x) return 32;
        unsigned long idx;
        _BitScanReverse(&idx, x);                 // 最上位1bitの位置
        return 31 - static_cast<int>(idx);        // 先頭の0ビット数
#else
        return x ? __builtin_clz(x) : 32;
#endif
    }

    static inline int clz64(uint64_t x) {
#if defined(_MSC_VER)  // MSVC / clang-cl
        if (!x) return 64;
        unsigned long idx;
#if defined(_M_X64)
        _BitScanReverse64(&idx, x);
        return 63 - static_cast<int>(idx);
#else
        // 32bit ターゲットのフォールバック
        if (x >> 32) {
            _BitScanReverse(&idx, static_cast<unsigned long>(x >> 32));
            return 31 - static_cast<int>(idx);
        } else {
            _BitScanReverse(&idx, static_cast<unsigned long>(x));
            return 63 - static_cast<int>(idx);
        }
#endif
#else
        return x ? __builtin_clzll(x) : 64;
#endif
    }
    
    void computeMortonCodes(const TrackingVector<ParticleData>& P){
      Vec3 ext = { wmax_.x - wmin_.x, wmax_.y - wmin_.y, wmax_.z - wmin_.z };
      if (ext.x==0) ext.x=1e-9f;
      if (ext.y==0) ext.y=1e-9f;
      if (ext.z==0) ext.z=1e-9f;
      
      const int N=(int)P.size();
      codes_.resize(N);
      for (int i=0;i<N;i++){
	Vec3 p01{ (P[i].pos[0] - wmin_.x)/ext.x,
		  (P[i].pos[1] - wmin_.y)/ext.y,
		  (P[i].pos[2] - wmin_.z)/ext.z };
	uint32_t qx=q01(p01.x,bits_), qy=q01(p01.y,bits_), qz=q01(p01.z,bits_);
	uint64_t m = morton3D(qx,qy,qz);
	codes_[i] = m;
      }
    }

    int lcp(int i,int j) const {
      if (j<0 || j>=(int)codesSorted_.size())
	return -1;

      uint64_t a = codesSorted_[i];
      uint64_t b = codesSorted_[j];

      if (a != b) {
        // Morton が異なる → 通常の共通接頭ビット長（0..63）
        return clz64(a ^ b);
      } else {
        // Morton が等しい → 「Morton の後ろにソート順位を連結した」とみなす
        // つまり 64bit ぶん一致 + index の共通上位ビット数（0..31）
        // これで δ(i,j) は 64..95 の範囲になり、等号連内でちゃんと序列がつく
        return 64 + clz32((uint32_t)i ^ (uint32_t)j);
      }
    }

    int findRangeEnd(int i, int dir, int dMin) const {
      const int N = (int)codesSorted_.size();

      // --- 指数探索: 最後に条件を満たした j と NG 側の bound を取る ---
      int j = i;        // last good
      int step = 1;
      int bound;        // first bad (or array end in that direction)
      while (true) {
        long long k = (long long)i + (long long)step * dir;
        if (k < 0) { bound = 0; break; }
        if (k >= N) { bound = N - 1; break; }
        if (lcp(i, (int)k) <= dMin) { bound = (int)k; break; }
        j = (int)k;  // still good
        step <<= 1;
      }

      //printf("[End] dMin=%d\n", dMin);
      
      // --- 二分探索 ---
      if (dir > 0) {
        // 右方向: 「最大の OK」を返す（上側二分探索）
        int lo = j;           // OK 側
        int hi = bound;       // NG 側 or 端（閉区間）
        while (lo < hi) {
	  int mid = lo + (hi - lo + 1) / 2;     // 上より
	  if (lcp(i, mid) > dMin) lo = mid;     // まだ OK を広げる
	  else hi = mid - 1;                    // NG を縮める
	}
	
        return lo; // 最大の OK
      } else {
        // 左方向: 「最小の OK」を返す（下側二分探索）
        int lo = bound;       // NG 側 or 端
        int hi = j;           // OK 側（閉区間）
        while (lo < hi) {
	  int mid = lo + (hi - lo) / 2;         // 下より
	  if (lcp(i, mid) > dMin) hi = mid;     // OK を左へ寄せる
	  else lo = mid + 1;                    // NG を右へ寄せる
        }
        return hi; // 最小の OK
      }
    }

    int findSplit(int i, int j) const {
      if (i == j)
	return i;
      
      int common = lcp(i, j);     // ★ i を基準に last との共通接頭長
      //printf("[split] common=%d i=%d j=%d\n", common, i, j);
      
      if(j > i){
	int lo = i, hi = j;	
	while (hi - lo > 1) {
	  int mid = (lo + hi) >> 1;
	  if (lcp(i, mid) > common) lo = mid;
	  else                      hi = mid;
	}
	//printf("[split] split=%d\n", lo);	
	return lo;	
      }else{
	int lo = j, hi = i;
	while (hi - lo > 1) {
	  int mid = (lo + hi) >> 1;
	  if (lcp(i, mid) > common) hi = mid;
	  else                      lo = mid;
	}

	//printf("[split] split=%d\n", lo);	
	return lo;	
      }
      
      // 二分探索：lcp(i, mid) > common を満たす最大の mid を探す
    }
    
    static void refit(std::vector<BVHNodeCPU>& nodes,int id, int N){
      BVHNodeCPU& n = nodes[id];
      
      if (n.left<0 && n.right<0) return; // leaf
      if (n.left >=0) refit(nodes, n.left, N);
      if (n.right>=0) refit(nodes, n.right, N);

      float len[3];
      for (int c=0;c<3;c++){
	nodes[id].bmin[c] = std::min(nodes[n.left].bmin[c], nodes[n.right].bmin[c]);
	nodes[id].bmax[c] = std::max(nodes[n.left].bmax[c], nodes[n.right].bmax[c]);
	len[c] = nodes[id].bmax[c] - nodes[id].bmin[c];
      }
      
      nodes[id].volume = len[0]*len[1]*len[2];
      nodes[id].bmin[3]=nodes[id].bmax[3]=0.0f;

      nodes[id].sigma_avg = 1.;
      nodes[id].sigma_max = 1.;
    }
    
  private:
    int bits_;
    int root_;
    std::vector<uint64_t> codes_, codesSorted_;
    Vec3 wmin_{}, wmax_{};
  };

  // ======================================================
  //                     BVH ラッパ
  // ======================================================
  class BVH {
  public:
    void build(const TrackingVector<ParticleData>& in, IBVHBuilder& builder){
      results_ = builder.build(in);
    }
    const std::vector<BVHNodeCPU>& nodes() const { return results_.nodes; }
    const std::vector<GpuParticle>& gpuParticles() const { return results_.gpu; }
    int root() const { return results_.root; }

  private:
    BuildResult results_;
  };

} // namespace lbvh
