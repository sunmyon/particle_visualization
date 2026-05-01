// ==================================================================
// DiskRadiusFinder (C++17)
// enclosed-mass Kepler test
// * float[3] position / velocity in SimulationElement
// * stateless finder: each compute() fills a DiskObject
// ==================================================================
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <Eigen/Core>
#include <Eigen/Geometry>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "render/scene_objects.h"
#include "data/sample_coordinates.h"

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
    float scale_fac   = 1.0f;
    double mass       = 0.0;
    size_t max_shell  = 100;   // Maximum number of shells.
    double f_cut      = 0.7;   // Kepler-ratio threshold.
    double G          = 1.0;   // Gravitation constant for the specified unit
  };

  DiskRadiusFinder() = default;

  template<class VecT>
  bool compute(const VecT& particles,
               float worldToRenderScale,
               const Params& par,
               DiskObject& disk) const;

private:
  struct PDisk {
    int type = 0;
    double m = 0.0;
    Eigen::Vector3d pos{0,0,0};
    Eigen::Vector3d vel{0,0,0};
  };

  static glm::quat rotationFromUpToNormal(const Eigen::Vector3d& Lhat)
  {
    const glm::vec3 up(0.f, 1.f, 0.f);
    glm::vec3 n = glm::normalize(glm::vec3(
      static_cast<float>(Lhat.x()),
      static_cast<float>(Lhat.y()),
      static_cast<float>(Lhat.z())
    ));

    float cosAng = glm::clamp(glm::dot(up, n), -1.f, 1.f);

    if (cosAng > 0.9999f) {
      return glm::quat(1.f, 0.f, 0.f, 0.f);
    }

    if (cosAng < -0.9999f) {
      return glm::angleAxis(glm::pi<float>(), glm::vec3(1.f, 0.f, 0.f));
    }

    glm::vec3 axis = glm::normalize(glm::cross(up, n));
    float angle = std::acos(cosAng);
    return glm::angleAxis(angle, axis);
  }
};

/*=================== Implementation =============================*/
template<class VecT>
inline bool DiskRadiusFinder::compute(const VecT& particles,
                                      float worldToRenderScale,
                                      const Params& par,
                                      DiskObject& disk) const
{
  const double Grav = par.G;

  disk.clear();
  disk.position = glm::vec3(par.center[0], par.center[1], par.center[2]);

  std::vector<PDisk> P;
  P.reserve(particles.size());

  for (const auto& src : particles) {
    const glm::vec3 pos = renderPosition(src, worldToRenderScale);
    PDisk d;
    d.m = src.mass;
    d.pos = {
      double(pos.x - par.center[0]),
      double(pos.y - par.center[1]),
      double(pos.z - par.center[2])
    };
    d.vel = {
      double(src.vel[0] - par.v_center[0]),
      double(src.vel[1] - par.v_center[1]),
      double(src.vel[2] - par.v_center[2])
    };
    d.type = src.type;
    P.push_back(d);
  }

  const size_t N = P.size();
  if (N == 0) return false;

  std::vector<std::pair<double, size_t>> buf;
  buf.reserve(N);

  for (size_t i = 0; i < N; ++i) {
    buf.emplace_back(P[i].pos.squaredNorm(), i);
  }

  PAR_SORT(buf.begin(), buf.end(),
           [](const auto& a, const auto& b) { return a.first < b.first; });

  double Mcum = 0.0;
  Eigen::Vector3d Lcum = Eigen::Vector3d::Zero();
  Eigen::Vector3d vcum = Eigen::Vector3d::Zero();
  size_t cursor = 0;

  const size_t index10 = (N < 100 ? N - 1 : 100);
  double r_i   = P[buf[index10].second].pos.norm();
  double r_max = P[buf[N - 1].second].pos.norm();

  if (r_i <= 0.0 || r_max <= r_i || par.max_shell == 0) {
    return false;
  }

  const double dln_r = std::log(r_max / r_i) / static_cast<double>(par.max_shell);

  std::printf("N=%zu r_min=%g max=%g dln_r=%g max_shell=%zu\n",
              N, r_i * par.scale_fac, r_max * par.scale_fac, dln_r, par.max_shell);

  int count_outside = 0;
  double r_i_old = 0.0;
  bool found = false;

  for (size_t shell = 0; shell < par.max_shell; ++shell, r_i *= std::exp(dln_r)) {
    for (; cursor < N; ++cursor) {
      const auto& p = P[buf[cursor].second];
      if (p.pos.norm() >= r_i) break;
      Mcum += p.m;
      vcum += p.m * p.vel;
    }

    if (Mcum == 0.0) continue;

    Eigen::Vector3d vmean = vcum / Mcum;

    Lcum = Eigen::Vector3d::Zero();
    for (size_t k = 0; k < cursor; ++k) {
      const auto& p = P[buf[k].second];
      Lcum += p.m * p.pos.cross(p.vel - vmean);
    }

    if (Lcum.norm() == 0.0) continue;
    Eigen::Vector3d Lhat = Lcum.normalized();

    Eigen::Matrix3d R;
    {
      Eigen::Vector3d z = Lhat;
      Eigen::Vector3d ref(0,0,1);
      if (std::fabs(z.dot(ref)) > 0.9) ref = {1,0,0};
      Eigen::Vector3d x = ref.cross(z).normalized();
      Eigen::Vector3d y = z.cross(x);
      R.col(0) = x;
      R.col(1) = y;
      R.col(2) = z;
    }

    double vphi_sum = 0.0;
    double m_sum = 0.0;

    for (size_t k = 0; k < cursor; ++k) {
      const auto& p = P[buf[k].second];
      if (p.type != 0) continue;
      if (p.pos.norm() < r_i_old) continue;

      Eigen::Vector3d rD = R.transpose() * p.pos;
      Eigen::Vector3d vD = R.transpose() * (p.vel - vmean);
      double Rxy = std::hypot(rD.x(), rD.y());
      if (Rxy == 0.0) continue;

      double vphi = (rD.x() * vD.y() - rD.y() * vD.x()) / Rxy;
      vphi_sum += p.m * vphi;
      m_sum    += p.m;
    }

    if (m_sum == 0.0) continue;

    double vphi_mean = vphi_sum / m_sum;
    double vK = std::sqrt(Grav * Mcum / r_i / par.scale_fac);
    double fK = vphi_mean / vK;

    std::printf("[%zu] r_i=%g mass=%g Mcum=%g v=%g vphi=%g f=%g\n",
                shell, r_i * par.scale_fac, par.mass, Mcum, vK, vphi_mean, fK);

    if (fK >= par.f_cut && fK < 1.5) {
      disk.set(glm::vec3(par.center[0], par.center[1], par.center[2]),
               static_cast<float>(r_i),
               rotationFromUpToNormal(Lhat));
      found = true;
      count_outside = 0;
    } else {
      ++count_outside;
      if (count_outside > 3) break;
    }

    r_i_old = r_i;
  }

  return found;
}
