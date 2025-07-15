// ==================================================================
// DiskRadiusFinder.hpp  (C++17) — enclosed‑mass Kepler test, ready for
// *   float[3] position / velocity in ParticleData
// *   reuse‑able object: ctor は軽量、毎回 compute() に粒子+Params を渡す
// ==================================================================
#pragma once
#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <Eigen/Core>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>   // translate / scale / rotate
#include <glm/gtc/constants.hpp>          // pi()

#if defined(USE_TBB) && __has_include(<tbb/parallel_sort.h>)
#define HAS_TBB 1
#include <tbb/parallel_sort.h>
#else
#define HAS_TBB 0
#include <algorithm>
#endif

#if HAS_TBB
#define PAR_SORT(first,last,comp) tbb::parallel_sort(first, last, comp)
#else
#define PAR_SORT(first,last,comp) std::sort(first, last, comp)
#endif

class DiskRadiusFinder {
public:
  struct Params {
    float center[3]   = {0,0,0};
    float v_center[3] = {0,0,0};
    float scale_fac;
    double mass;
    size_t max_shell  = 100;          // 殻数上限
    double f_cut      = 0.7;          // Kepler 比閾値
    double G;                         //Gravitation constatn for the specified unit
  };

  /* ------- 結果を保持 (compute ごと更新) ------- */
  double Rdisk   = 0.0;
  double Mdisk   = 0.0;
  double f_last  = 0.0;
  double Rcenter[3];
  Eigen::Vector3d Lhat_last{0,0,1};

  DiskRadiusFinder() = default;

  template<class VecT>
  void compute(const VecT& particles, const Params& par);

  /* 半径と法線を取得するヘルパ */
  double getDiskRadius(float* normal=nullptr) const {
    if(normal){
      normal[0] = static_cast<float>(Lhat_last.x());
      normal[1] = static_cast<float>(Lhat_last.y());
      normal[2] = static_cast<float>(Lhat_last.z());
    }
    return Rdisk;
  }

  glm::mat4 getModelMatrix(void) const
  {
    /* 0. 基本量 --------------------------------------------------- */
    glm::vec3 center{ static_cast<float>(Rcenter[0]),
                      static_cast<float>(Rcenter[1]),
                      static_cast<float>(Rcenter[2]) };

    glm::vec3 n = glm::normalize(glm::vec3(  float(Lhat_last.x()),
                                             float(Lhat_last.y()),
                                             float(Lhat_last.z())  ));

    const glm::vec3 up(0.f, 1.f, 0.f);        // 元メッシュの法線
    const float R     = static_cast<float>(Rdisk);
    const float thicknessRatio = 0.1;
    const float halfH = R * thicknessRatio;   // 片側厚み

    /* 1. 回転行列  R ---------------------------------------------- */
    glm::mat4 Rmat(1.f);

    float cosAng = glm::clamp(glm::dot(up, n), -1.f, 1.f);
    if (cosAng < 0.9999f)                        // up ≠ n
      {
        if (cosAng > -0.9999f)                  // 一般角度
	  {
            glm::vec3 axis = glm::normalize(glm::cross(up, n));
            float angle = std::acos(cosAng);    // [0..π]
            Rmat = glm::rotate(glm::mat4(1.f), angle, axis);
	  }
        else                                    // 反平行: π 回転
	  {
            Rmat = glm::rotate(glm::mat4(1.f), glm::pi<float>(),
                               glm::vec3(1.f, 0.f, 0.f)); // 任意に X 軸
	  }
      }
    /* cosAng ≈1 のときは R=I のまま */

    /* 2. スケール & 平行移動 -------------------------------------- */
    glm::mat4 S = glm::scale(glm::mat4(1.f), glm::vec3(R, halfH, R));
    glm::mat4 T = glm::translate(glm::mat4(1.f), center);

    /* 3. モデル行列 = T · R · S ----------------------------------- */
    return T * Rmat * S;     // スケール → 回転 → 平行移動
  }
  
private:
  struct PDisk {
    int type;
    double m;
    Eigen::Vector3d pos;
    Eigen::Vector3d vel;
  };

  /* ワークバッファ: 毎 compute で再利用しメンバに保持しない */
};

/*=================== 実装 =======================================*/
template<class VecT>
inline void DiskRadiusFinder::compute(const VecT& particles, const Params& par)
{
  double Grav = par.G;
  Rcenter[0] = static_cast<double>(par.center[0]);
  Rcenter[1] = static_cast<double>(par.center[1]);
  Rcenter[2] = static_cast<double>(par.center[2]);
  
  /* ---------- 0. ParticleData → PDisk (+中心補正) ------------- */
  std::vector<PDisk> P;
  P.reserve(particles.size());
  for(const auto& src: particles){
    PDisk d;
    d.m = src.mass;
    d.pos = { double(src.pos[0] - par.center[0]),
	      double(src.pos[1] - par.center[1]),
	      double(src.pos[2] - par.center[2]) };
    d.vel = { double(src.vel[0] - par.v_center[0]),
	      double(src.vel[1] - par.v_center[1]),
	      double(src.vel[2] - par.v_center[2]) };
    d.type = src.type;
    
    P.push_back(d);
  }
  const size_t N = P.size();

  /* ---------- 1. |r| 昇順インデックス ------------------------ */
  std::vector<std::pair<double, size_t>> buf;
  buf.reserve(N);
  
  for (size_t i = 0; i < N; ++i)
    buf.emplace_back(P[i].pos.squaredNorm(), i);   // ← r² と ID
  
  /* 2) key で昇順ソート（コピー抑止のため const & 推奨）*/
  PAR_SORT(buf.begin(), buf.end(),
	   [](const auto& a, const auto& b){ return a.first < b.first; });
  
  /* ---------- 2. 殻スキャン ---------------------------------- */
  double Mcum = 0.0;
  Eigen::Vector3d Lcum = Eigen::Vector3d::Zero();
  Eigen::Vector3d vcum = Eigen::Vector3d::Zero();
  size_t cursor = 0;

  int index10 = (N < 100?N:100);
  double r_i = P[buf[index10].second].pos.norm();
  double r_max = P[buf[N-1].second].pos.norm();
  double dln_r = log(r_max/r_i) / static_cast<double>(par.max_shell);
  
  Rdisk = 0; Mdisk = 0; f_last = 0; Lhat_last = {0,0,1};
  printf("N=%zu r_min=%g max=%g dln_r=%g max_shell=%zu\n", N, r_i*par.scale_fac, r_max*par.scale_fac, dln_r, par.max_shell);

  int count_outside = 0;
  double r_i_old = 0.;
  
  for(size_t shell=0; shell<par.max_shell; ++shell, r_i*=std::exp(dln_r))
    {
      /* 2-a) 累積追加 */
      for(; cursor<N; ++cursor){
	const auto& p = P[buf[cursor].second];
	if(p.pos.norm() >= r_i) break;
	Mcum += p.m;
	vcum += p.m * p.vel;
      }
      if(Mcum==0.0) continue;            // 空殻

      Eigen::Vector3d vmean = vcum / Mcum;

      Lcum = Eigen::Vector3d::Zero();
      for(size_t k=0;k<cursor;++k){
	const auto& p=P[buf[k].second];
	Lcum += p.m * p.pos.cross(p.vel-vmean);
      }
      
      Eigen::Vector3d Lhat = Lcum.normalized();
      /* 回転行列 */
      Eigen::Matrix3d R;
      {
	Eigen::Vector3d z=Lhat;
	Eigen::Vector3d ref(0,0,1);
	if(std::fabs(z.dot(ref))>0.9) ref={1,0,0};
	Eigen::Vector3d x=ref.cross(z).normalized();
	Eigen::Vector3d y=z.cross(x);
	R.col(0)=x; R.col(1)=y; R.col(2)=z;
      }

      /* 2-b) 平均 v_phi */
      double vphi_sum=0, m_sum=0;
      for(size_t k=0;k<cursor;++k){	
	const auto& p=P[buf[k].second];
	if(p.type != 0)
	  continue;

	if(p.pos.norm() < r_i_old)
	  continue;
	
	Eigen::Vector3d rD = R.transpose()*p.pos;
	Eigen::Vector3d vD = R.transpose()*(p.vel - vmean);
	double Rxy = std::hypot(rD.x(), rD.y());
	if(Rxy==0) continue;
	double vphi = (rD.x()*vD.y() - rD.y()*vD.x())/Rxy;
	vphi_sum += p.m * vphi;
	m_sum    += p.m;
      }
      
      double vphi_mean = vphi_sum / m_sum;
      double vK = std::sqrt(Grav * Mcum / r_i / par.scale_fac);
      double fK = vphi_mean / vK;

      printf("[%zu] r_i=%g mass=%g Mcum=%g v=%g vphi=%g f=%g\n"
	     , shell, r_i*par.scale_fac, par.mass, Mcum, vK, vphi_mean, fK);

      if(fK >= par.f_cut && fK < 1.5){
	f_last = fK;
	Rdisk = r_i;
	Mdisk = Mcum;
	Lhat_last = Lhat;
	count_outside = 0;
      }else{     
	count_outside++;
	if(count_outside > 3)
	  return;
      }

      r_i_old = r_i;
    }
}
