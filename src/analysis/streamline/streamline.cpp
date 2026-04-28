#include <cstdio>

#include <Eigen/Core>
#include <nanoflann.hpp>

#include "analysis/streamline/streamline.h"


namespace {

inline float clamp_positive(float x, float lo, float hi) {
  return std::max(lo, std::min(x, hi));
}

inline Eigen::Vector3f toEigen(const float p[3]) {
  return Eigen::Vector3f(p[0], p[1], p[2]);
}

inline float limiter_ratio(float dq_allowed, float dq_trial) {
  if (dq_trial <= 0.0f) return 1.0f;
  return std::clamp(dq_allowed / dq_trial, 0.0f, 1.0f);
}

}  // namespace

void StreamlineComputer::setRegionFromParticleData(TrackingVector<ParticleData>& particles) {
  if (flagSetRegionByHand) return;

  for (int k = 0; k < 3; ++k) {
    xmin_seed[k] = 1.e30f;
    xmax_seed[k] = -1.e30f;
  }

  for (auto& p : particles) {
    if (p.type != 0) continue;
    for (int k = 0; k < 3; ++k) {
      xmin_seed[k] = std::min(xmin_seed[k], p.pos[k]);
      xmax_seed[k] = std::max(xmax_seed[k], p.pos[k]);
    }
  }
}

void StreamlineComputer::setRegionByHand(float* center, float* len) {
  for (int k = 0; k < 3; ++k) {
    xmin_seed[k] = center[k] - 0.5f * len[k];
    xmax_seed[k] = center[k] + 0.5f * len[k];
  }
  flagSetRegionByHand = true;
}

void StreamlineComputer::setStreamRegionFromParticleData(TrackingVector<ParticleData>& particles) {
  if (flagSetStreamRegionByHand) return;

  for (int k = 0; k < 3; ++k) {
    xmin_[k] = 1.e30f;
    xmax_[k] = -1.e30f;
  }

  for (auto& p : particles) {
    if (p.type != 0) continue;
    for (int k = 0; k < 3; ++k) {
      xmin_[k] = std::min(xmin_[k], p.pos[k]);
      xmax_[k] = std::max(xmax_[k], p.pos[k]);
    }
  }
}

void StreamlineComputer::setStreamRegionByHand(float* center, float* len) {
  for (int k = 0; k < 3; ++k) {
    xmin_[k] = center[k] - 0.5f * len[k];
    xmax_[k] = center[k] + 0.5f * len[k];
  }
  flagSetStreamRegionByHand = true;
}

void StreamlineComputer::setSeeds(TrackingVector<ParticleData>& particles, int n_seeds) {
  if (n_seeds <= 0) n_seeds = 1;

  std::vector<size_t> selected_indices;
  selected_indices.reserve(particles.size());

  for (size_t i = 0; i < particles.size(); ++i) {
    auto& p = particles[i];
    if (p.type != 0) continue;
    if (p.pos[0] < xmin_seed[0] || p.pos[0] > xmax_seed[0]) continue;
    if (p.pos[1] < xmin_seed[1] || p.pos[1] > xmax_seed[1]) continue;
    if (p.pos[2] < xmin_seed[2] || p.pos[2] > xmax_seed[2]) continue;
    selected_indices.push_back(i);
  }

  m_seeds.clear();
  m_hsmls.clear();

  if (selected_indices.empty()) return;

  const int n_actual = std::min<int>(n_seeds, selected_indices.size());
  m_seeds.reserve(n_actual);
  m_hsmls.reserve(n_actual);

  for (int i = 0; i < n_actual; ++i) {
    const size_t idx = static_cast<size_t>((double)i * selected_indices.size() / n_actual);
    const auto& p = particles[selected_indices[idx]];

    m_seeds.push_back({p.pos[0], p.pos[1], p.pos[2]});
    m_hsmls.push_back(std::max(1.0e-6f, p.Hsml));
  }
}

std::vector<LineObject> StreamlineComputer::build(ParticleBlock& particles, double theta_max_in_degree) {
  estimate_gradB(particles);

  const float theta_max = static_cast<float>(theta_max_in_degree * 3.14159265358979323846 / 180.0);

  m_lines.clear();
  m_lines.reserve(m_seeds.size());

  for (size_t i = 0; i < m_seeds.size(); ++i) {
    const float h_init = std::max(1.0e-6f, 0.1f * m_hsmls[i]);
    auto line = integrateBiStreamline(m_seeds[i], h_init, MaxStep);
    m_lines.push_back(sampleByCurvature(line, theta_max));
  }

  std::vector<LineObject> out;
  out.reserve(m_lines.size());

  for (const auto& linePts : m_lines) {
    if (linePts.empty()) continue;

    LineObject obj;
    obj.points.reserve(linePts.size());

    for (const auto& p : linePts) 
      obj.points.emplace_back(p.x, p.y, p.z);    

    out.push_back(std::move(obj));
  }

  return out;
}

void StreamlineComputer::estimate_gradB(ParticleBlock& particleBlock) {
  TrackingVector<particle_stream> p_stream;
  p_stream.reserve(particleBlock.particles.size());

  const bool flag_Bfield = particleBlock.hasSoAAs(soa_views::Bfield);

  for (size_t i = 0; i < particleBlock.particles.size(); ++i) {
    ParticleData& p = particleBlock.particles[i];
    if (p.type != 0) continue;
    if (p.pos[0] < xmin_[0] || p.pos[0] > xmax_[0]) continue;
    if (p.pos[1] < xmin_[1] || p.pos[1] > xmax_[1]) continue;
    if (p.pos[2] < xmin_[2] || p.pos[2] > xmax_[2]) continue;

    particle_stream ps{};
    ps.pos[0] = p.pos[0];
    ps.pos[1] = p.pos[1];
    ps.pos[2] = p.pos[2];
    ps.cell_size = std::max(1.0e-6f, p.Hsml);

    if (flag_Bfield) {
      float bf[3] = {0.0f, 0.0f, 0.0f};
      if (particleBlock.readSoAAs(soa_views::Bfield, i, bf)) {
        ps.vect[0] = bf[0];
        ps.vect[1] = bf[1];
        ps.vect[2] = bf[2];
      }
    } else {
      ps.vect[0] = p.vel[0];
      ps.vect[1] = p.vel[1];
      ps.vect[2] = p.vel[2];
    }

    p_stream.push_back(ps);
  }

  cloud.particles = p_stream;

  if (cloud.particles.empty()) {
    m_kdTree.reset();
    gradB.clear();
    gradLimiter.clear();
    r_neighbours.clear();
    return;
  }

  m_kdTree.reset(new KDTreeType(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10)));
  m_kdTree->buildIndex();

  gradB.clear();
  gradB.resize(cloud.particles.size());
  gradLimiter.clear();
  gradLimiter.resize(cloud.particles.size(), Eigen::Vector3f::Ones());
  r_neighbours.clear();
  r_neighbours.resize(cloud.particles.size(), 0.0f);

  std::array<IndexType, N_neighbours> idx_buf{};
  std::array<DistanceType, N_neighbours> dist2_buf{};

  for (size_t i = 0; i < cloud.particles.size(); ++i) {
    const auto& pi = cloud.particles[i];
    const size_t n_found = m_kdTree->knnSearch(pi.pos, N_neighbours, idx_buf.data(), dist2_buf.data());
    if (n_found == 0) {
      gradB[i][0].setZero();
      gradB[i][1].setZero();
      gradB[i][2].setZero();
      gradLimiter[i].setOnes();
      continue;
    }

    r_neighbours[i] = std::sqrt(dist2_buf[n_found - 1]);

    const float sigma = std::max(0.5f * pi.cell_size, r_neighbours[i]);
    const float sigma2 = sigma * sigma;

    Eigen::Matrix3f M = Eigen::Matrix3f::Zero();
    Eigen::Vector3f bx = Eigen::Vector3f::Zero();
    Eigen::Vector3f by = Eigen::Vector3f::Zero();
    Eigen::Vector3f bz = Eigen::Vector3f::Zero();

    float bmin[3] = {pi.vect[0], pi.vect[1], pi.vect[2]};
    float bmax[3] = {pi.vect[0], pi.vect[1], pi.vect[2]};

    for (size_t n = 0; n < n_found; ++n) {
      const size_t j = idx_buf[n];
      if (j == i) continue;

      const auto& pj = cloud.particles[j];
      const Eigen::Vector3f d(
          pj.pos[0] - pi.pos[0],
          pj.pos[1] - pi.pos[1],
          pj.pos[2] - pi.pos[2]);

      const float r2 = d.squaredNorm();
      const float w = std::exp(-r2 / std::max(1.0e-20f, sigma2));

      M += w * (d * d.transpose());
      bx += w * d * (pj.vect[0] - pi.vect[0]);
      by += w * d * (pj.vect[1] - pi.vect[1]);
      bz += w * d * (pj.vect[2] - pi.vect[2]);

      bmin[0] = std::min(bmin[0], pj.vect[0]);
      bmax[0] = std::max(bmax[0], pj.vect[0]);
      bmin[1] = std::min(bmin[1], pj.vect[1]);
      bmax[1] = std::max(bmax[1], pj.vect[1]);
      bmin[2] = std::min(bmin[2], pj.vect[2]);
      bmax[2] = std::max(bmax[2], pj.vect[2]);
    }

    const float reg = GradRegFraction * (M.trace() + 1.0e-20f);
    M += reg * Eigen::Matrix3f::Identity();

    Eigen::LDLT<Eigen::Matrix3f> solver(M);
    if (solver.info() != Eigen::Success) {
      gradB[i][0].setZero();
      gradB[i][1].setZero();
      gradB[i][2].setZero();
      gradLimiter[i].setOnes();
      continue;
    }

    gradB[i][0] = solver.solve(bx);
    gradB[i][1] = solver.solve(by);
    gradB[i][2] = solver.solve(bz);

    Eigen::Vector3f phi = Eigen::Vector3f::Ones();

    for (size_t n = 0; n < n_found; ++n) {
      const size_t j = idx_buf[n];
      if (j == i) continue;

      const auto& pj = cloud.particles[j];
      const Eigen::Vector3f d(
          pj.pos[0] - pi.pos[0],
          pj.pos[1] - pi.pos[1],
          pj.pos[2] - pi.pos[2]);

      for (int c = 0; c < 3; ++c) {
        const float qi = pi.vect[c];
        const float dq = gradB[i][c].dot(d);

        if (dq > LimiterEps) {
          phi[c] = std::min(phi[c], limiter_ratio(bmax[c] - qi, dq));
        } else if (dq < -LimiterEps) {
          phi[c] = std::min(phi[c], limiter_ratio(qi - bmin[c], -dq));
        }
      }
    }

    gradB[i][0] *= phi[0];
    gradB[i][1] *= phi[1];
    gradB[i][2] *= phi[2];
    gradLimiter[i] = phi;
  }
}

bool StreamlineComputer::evalFieldAt(const float x[3], float outB[3], float& hsml) const {
  outB[0] = outB[1] = outB[2] = 0.0f;
  hsml = 0.0f;

  if (!m_kdTree || cloud.particles.empty()) return false;

  std::array<IndexType, N_neighbours> idx_buf{};
  std::array<DistanceType, N_neighbours> dist2_buf{};
  const size_t n_found = m_kdTree->knnSearch(x, N_neighbours, idx_buf.data(), dist2_buf.data());
  if (n_found == 0) return false;

  hsml = std::max(1.0e-6f, std::sqrt(dist2_buf[0]));

  const Eigen::Vector3f xe = toEigen(x);
  Eigen::Vector3f Bsum = Eigen::Vector3f::Zero();
  float sumw = 0.0f;

  for (size_t n = 0; n < 1; ++n) {
    const size_t id = idx_buf[n];
    const auto& p = cloud.particles[id];

    const Eigen::Vector3f pi = toEigen(p.pos);
    const Eigen::Vector3f d = xe - pi;

    Eigen::Vector3f Bi(p.vect[0], p.vect[1], p.vect[2]);
    //Bi[0] += g[0].dot(d);
    //Bi[1] += g[1].dot(d);
    //Bi[2] += g[2].dot(d);

    const float sigma = std::max(0.5f * p.cell_size, hsml);
    const float sigma2 = sigma * sigma;
    const float r2 = d.squaredNorm();
    const float w = std::exp(-r2 / std::max(1.0e-20f, sigma2));

    printf("id=%zu vec=%g %g %g\n", id, Bi[0], Bi[1], Bi[2]);
    
    Bsum += w * Bi;
    sumw += w;
  }

  if (sumw <= 0.0f) return false;

  const Eigen::Vector3f B = Bsum / sumw;
  outB[0] = B[0];
  outB[1] = B[1];
  outB[2] = B[2];
  return true;
}

bool StreamlineComputer::evalDirectionAt(const Vec3& x, Vec3& dir, float& hsml) const {
  float buf[3] = {0.0f, 0.0f, 0.0f};
  if (!evalFieldAt(&x.x, buf, hsml)) {
    dir = {0.0f, 0.0f, 0.0f};
    return false;
  }

  const Vec3 B{buf[0], buf[1], buf[2]};
  const float b = len(B);
  if (b <= WeakFieldFloor) {
    dir = {0.0f, 0.0f, 0.0f};
    return false;
  }

  dir = B / b;
  return true;
}

bool StreamlineComputer::RK4stepArcLength(const Vec3& x, float& h, float sign, Vec3& x_next) const {
  Vec3 k1, k2, k3, k4;
  float hsml1 = 0.0f, hsml2 = 0.0f, hsml3 = 0.0f, hsml4 = 0.0f;

  if (!evalDirectionAt(x, k1, hsml1)) return false;

  const float h_abs_prev = std::abs(h);
  const float h_abs = clamp_positive(
      0.15f * hsml1,
      std::max(1.0e-6f, MinStepFrac * hsml1),
      std::max(2.0e-6f, MaxStepFrac * hsml1));

  const float ds = (h_abs_prev > 0.0f)
      ? std::copysign(std::min(std::max(h_abs_prev, 0.5f * h_abs), 2.0f * h_abs), sign)
      : std::copysign(h_abs, sign);

  if (!evalDirectionAt(x + k1 * (0.5f * ds), k2, hsml2)) return false;
  if (!evalDirectionAt(x + k2 * (0.5f * ds), k3, hsml3)) return false;
  if (!evalDirectionAt(x + k3 * ds, k4, hsml4)) return false;

  x_next = x + (k1 + k2 * 2.0f + k3 * 2.0f + k4) * (ds / 6.0f);
  h = ds;
  return true;
}

std::vector<Vec3> StreamlineComputer::integrateStreamline(const Vec3& seed, float h_init, int maxSteps, float sign) const {
  (void)h_init;
  std::vector<Vec3> line;
  line.reserve(std::min(maxSteps, 4096));

  Vec3 x = seed;

  if (!is_inside_(x)) return line;

  for (int i = 0; i < maxSteps; ++i) {
    line.push_back(x);

    if(i%1 == 0)
      printf("[%d] x=%g %g %g\n", i, x.x, x.y, x.z);
    
    Vec3 x_next;
    //if (!RK4stepArcLength(x, h, sign, x_next)) break;

    Vec3 k1;
    float hsml1 = 0.;
    evalDirectionAt(x, k1, hsml1);
    const float ds = sign * 0.01f * hsml1;
    x_next.x = x.x + ds * k1.x;
    x_next.y = x.y + ds * k1.y;
    x_next.z = x.z + ds * k1.z;
    
    if (!is_inside_(x_next)) break;

    const Vec3 dx = x_next - x;
    if (len2(dx) <= 1.0e-24f) break;

    x = x_next;
  }

  return line;
}

std::vector<Vec3> StreamlineComputer::integrateBiStreamline(const Vec3& seed, float h_init, int maxSteps) const {
  auto backward = integrateStreamline(seed, h_init, maxSteps, -1.0f);
  auto forward = integrateStreamline(seed, h_init, maxSteps, +1.0f);

  std::vector<Vec3> fullLine;
  if (!backward.empty()) {
    std::reverse(backward.begin(), backward.end());
    fullLine.insert(fullLine.end(), backward.begin(), backward.end());
    if (!fullLine.empty()) fullLine.pop_back();
  }
  fullLine.insert(fullLine.end(), forward.begin(), forward.end());
  return fullLine;
}

bool StreamlineComputer::is_inside_(const Vec3& x) const {
  if (x.x < xmin_[0] || x.x > xmax_[0]) return false;
  if (x.y < xmin_[1] || x.y > xmax_[1]) return false;
  if (x.z < xmin_[2] || x.z > xmax_[2]) return false;
  return true;
}

std::vector<Vec3> StreamlineComputer::sampleByCurvature(const std::vector<Vec3>& fullLine, float theta_max_rad) const {
  std::vector<Vec3> out;
  const size_t N = fullLine.size();
  if (N == 0) return out;

  out.push_back(fullLine[0]);
  for (size_t i = 1; i + 1 < N; ++i) {
    const Vec3 v1 = fullLine[i] - out.back();
    const Vec3 v2 = fullLine[i + 1] - fullLine[i];
    const float l1 = len(v1);
    const float l2 = len(v2);
    if (l1 < 1.0e-12f || l2 < 1.0e-12f) continue;

    float cosTh = dot(v1, v2) / (l1 * l2);
    cosTh = std::clamp(cosTh, -1.0f, 1.0f);
    const float theta = std::acos(cosTh);
    if (theta > theta_max_rad) out.push_back(fullLine[i]);
  }

  if (N > 1) out.push_back(fullLine.back());
  return out;
}
