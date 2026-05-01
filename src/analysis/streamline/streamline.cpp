#include <Eigen/Core>
#include <nanoflann.hpp>

#include <array>
#include <limits>
#include <sstream>

#include "analysis/streamline/streamline.h"
#include "data/sample_coordinates.h"


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

inline int stopReasonIndex(StreamlineStopReason reason) {
  return static_cast<int>(reason);
}

const char* stopReasonName(StreamlineStopReason reason) {
  switch (reason) {
    case StreamlineStopReason::None: return "none";
    case StreamlineStopReason::SeedOutsideRegion: return "seed outside region";
    case StreamlineStopReason::FieldEvalFailed: return "field eval failed";
    case StreamlineStopReason::WeakField: return "weak field";
    case StreamlineStopReason::OutOfBounds: return "out of bounds";
    case StreamlineStopReason::ZeroStep: return "zero step";
    case StreamlineStopReason::MaxStepsReached: return "max steps reached";
  }
  return "unknown";
}

float polylineLength(const std::vector<Vec3>& points) {
  float total = 0.0f;
  for (size_t i = 1; i < points.size(); ++i) {
    total += len(points[i] - points[i - 1]);
  }
  return total;
}

}  // namespace

StreamlineComputer::Bounds
StreamlineComputer::makeGasBounds(const std::vector<SimulationElement>& particles,
                                  float worldToRenderScale) const
{
  Bounds bounds;
  for (int k = 0; k < 3; ++k) {
    bounds.min[k] = 1.0e30f;
    bounds.max[k] = -1.0e30f;
  }

  for (const auto& p : particles) {
    if (p.type != 0) continue;
    const glm::vec3 pos = renderPosition(p, worldToRenderScale);
    for (int k = 0; k < 3; ++k) {
      bounds.min[k] = std::min(bounds.min[k], pos[k]);
      bounds.max[k] = std::max(bounds.max[k], pos[k]);
    }
    bounds.valid = true;
  }
  return bounds;
}

StreamlineComputer::Bounds
StreamlineComputer::makeBoxBounds(const StreamlineBoxSpec& box) const
{
  Bounds bounds;
  if (!box.enabled ||
      box.size[0] <= 0.0f ||
      box.size[1] <= 0.0f ||
      box.size[2] <= 0.0f) {
    return bounds;
  }

  for (int k = 0; k < 3; ++k) {
    bounds.min[k] = box.center[k] - 0.5f * box.size[k];
    bounds.max[k] = box.center[k] + 0.5f * box.size[k];
  }
  bounds.valid = true;
  return bounds;
}

std::vector<StreamlineComputer::SeedPoint>
StreamlineComputer::selectSeeds(const std::vector<SimulationElement>& particles,
                                float worldToRenderScale,
                                const Bounds& bounds,
                                int nSeeds) const
{
  if (!bounds.valid) return {};
  if (nSeeds <= 0) nSeeds = 1;

  std::vector<size_t> selected_indices;
  selected_indices.reserve(particles.size());

  for (size_t i = 0; i < particles.size(); ++i) {
    const auto& p = particles[i];
    if (p.type != 0) continue;
    const glm::vec3 pos = renderPosition(p, worldToRenderScale);
    if (pos[0] < bounds.min[0] || pos[0] > bounds.max[0]) continue;
    if (pos[1] < bounds.min[1] || pos[1] > bounds.max[1]) continue;
    if (pos[2] < bounds.min[2] || pos[2] > bounds.max[2]) continue;
    selected_indices.push_back(i);
  }

  if (selected_indices.empty()) return {};

  const int n_actual = std::min<int>(nSeeds, selected_indices.size());
  std::vector<SeedPoint> seeds;
  seeds.reserve(n_actual);

  for (int i = 0; i < n_actual; ++i) {
    const size_t idx = static_cast<size_t>((double)i * selected_indices.size() / n_actual);
    const auto& p = particles[selected_indices[idx]];
    const glm::vec3 pos = renderPosition(p, worldToRenderScale);

    seeds.push_back({{pos.x, pos.y, pos.z},
                     std::max(1.0e-6f,
                              renderSupportRadius(p, worldToRenderScale))});
  }
  return seeds;
}

StreamlineComputer::SeedPoint
StreamlineComputer::makeManualSeed(const std::vector<SimulationElement>& particles,
                                   float worldToRenderScale,
                                   const std::array<float, 3>& seed) const
{
  float nearestDist2 = std::numeric_limits<float>::max();
  float hsml = 1.0e-6f;

  for (const auto& p : particles) {
    if (p.type != 0) continue;
    const glm::vec3 pos = renderPosition(p, worldToRenderScale);
    const float dx = pos.x - seed[0];
    const float dy = pos.y - seed[1];
    const float dz = pos.z - seed[2];
    const float dist2 = dx * dx + dy * dy + dz * dz;
    if (dist2 < nearestDist2) {
      nearestDist2 = dist2;
      hsml = std::max(1.0e-6f, renderSupportRadius(p, worldToRenderScale));
    }
  }

  return {{seed[0], seed[1], seed[2]}, hsml};
}

StreamlineBuildOutput
StreamlineComputer::build(const SimulationBlock& particles,
                          const StreamlineBuildSpec& spec)
{
  StreamlineBuildOutput output;

  const float worldToRenderScale = particles.worldToRenderScale;
  const Bounds gasBounds = makeGasBounds(particles.particles, worldToRenderScale);
  if (!gasBounds.valid) {
    output.message = "No gas particles are available for streamline seeds.";
    return output;
  }

  Bounds seedBounds = makeBoxBounds(spec.seedRegion);
  if (!seedBounds.valid) seedBounds = gasBounds;

  Bounds fieldBounds = makeBoxBounds(spec.fieldRegion);
  if (!fieldBounds.valid) fieldBounds = gasBounds;

  std::vector<SeedPoint> seeds;
  if (spec.useManualSeed) {
    for (const auto& seed : spec.manualSeeds) {
      seeds.push_back(makeManualSeed(particles.particles, worldToRenderScale, seed));
    }
  } else {
    seeds = selectSeeds(particles.particles, worldToRenderScale, seedBounds, spec.nSeeds);
  }
  if (seeds.empty()) {
    output.message = "No seed particles were found in the selected seed region.";
    return output;
  }

  if (spec.fieldSource == StreamlineBuildSpec::FieldSource::BField &&
      !particles.hasSoAAs(soa_views::Bfield)) {
    output.message = "B field was requested, but this snapshot has no Bfield data.";
    return output;
  }

  estimate_gradB(particles, fieldBounds, spec.fieldSource);
  if (!m_kdTree || cloud.particles.empty()) {
    output.message = "No field particles were found in the selected streamline region.";
    return output;
  }

  const float theta_max =
    static_cast<float>(spec.thetaMaxDegrees * 3.14159265358979323846 / 180.0);
  const int maxSteps = std::max(1, spec.maxSteps);
  const float stepScale = std::max(1.0e-4f, spec.stepScale);

  output.seedCount = static_cast<int>(seeds.size());
  output.lines.reserve(seeds.size());

  output.seedReports.reserve(seeds.size());
  for (size_t seedIndex = 0; seedIndex < seeds.size(); ++seedIndex) {
    const auto& seed = seeds[seedIndex];
    const float h_init = std::max(1.0e-6f, 0.1f * seed.hsml);
    auto trace = integrateBiStreamline(seed.pos, h_init, maxSteps, stepScale);
    ++output.stopCounts[stopReasonIndex(trace.stopReason)];

    auto sampled = sampleByCurvature(trace.points, theta_max);
    StreamlineSeedReport report;
    report.seedIndex = static_cast<int>(seedIndex);
    report.position[0] = seed.pos.x;
    report.position[1] = seed.pos.y;
    report.position[2] = seed.pos.z;
    report.stopReason = trace.stopReason;
    report.pointCount = static_cast<int>(sampled.size());
    report.length = polylineLength(sampled);
    output.seedReports.push_back(report);

    if (!sampled.empty()) {
      output.lines.push_back(std::move(sampled));
    }
  }

  output.ok = !output.lines.empty();
  output.lineCount = static_cast<int>(output.lines.size());
  std::ostringstream msg;
  msg << (output.ok ? "Streamline build completed" : "No visible streamlines")
      << " (lines=" << output.lineCount
      << ", seeds=" << output.seedCount
      << "). Stops:";
  bool anyStop = false;
  for (int i = 0; i < static_cast<int>(output.stopCounts.size()); ++i) {
    if (output.stopCounts[i] == 0) continue;
    anyStop = true;
    msg << " " << stopReasonName(static_cast<StreamlineStopReason>(i))
        << "=" << output.stopCounts[i] << ";";
  }
  if (!anyStop) msg << " none=0;";
  output.message = msg.str();
  return output;
}

void StreamlineComputer::estimate_gradB(const SimulationBlock& simulationBlock,
                                        const Bounds& fieldBounds,
                                        StreamlineBuildSpec::FieldSource fieldSource) {
  fieldBounds_ = fieldBounds;

  std::vector<particle_stream> p_stream;
  p_stream.reserve(simulationBlock.particles.size());

  const bool useBfield = fieldSource == StreamlineBuildSpec::FieldSource::BField;

  for (size_t i = 0; i < simulationBlock.particles.size(); ++i) {
    const SimulationElement& p = simulationBlock.particles[i];
    if (p.type != 0) continue;
    const glm::vec3 pos = renderPosition(p, simulationBlock.worldToRenderScale);
    if (pos[0] < fieldBounds.min[0] || pos[0] > fieldBounds.max[0]) continue;
    if (pos[1] < fieldBounds.min[1] || pos[1] > fieldBounds.max[1]) continue;
    if (pos[2] < fieldBounds.min[2] || pos[2] > fieldBounds.max[2]) continue;

    particle_stream ps{};
    ps.pos[0] = pos.x;
    ps.pos[1] = pos.y;
    ps.pos[2] = pos.z;
    ps.cell_size =
      std::max(1.0e-6f, renderSupportRadius(p, simulationBlock.worldToRenderScale));

    if (useBfield) {
      float bf[3] = {0.0f, 0.0f, 0.0f};
      if (simulationBlock.readSoAAs(soa_views::Bfield, i, bf)) {
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

  for (size_t n = 0; n < n_found; ++n) {
    const size_t id = idx_buf[n];
    const auto& p = cloud.particles[id];

    const Eigen::Vector3f pi = toEigen(p.pos);
    const Eigen::Vector3f d = xe - pi;

    Eigen::Vector3f Bi(p.vect[0], p.vect[1], p.vect[2]);
    if (id < gradB.size()) {
      const auto& g = gradB[id];
      Bi[0] += g[0].dot(d);
      Bi[1] += g[1].dot(d);
      Bi[2] += g[2].dot(d);
    }

    const float sigma = std::max(0.5f * p.cell_size, hsml);
    const float sigma2 = sigma * sigma;
    const float r2 = d.squaredNorm();
    const float w = std::exp(-r2 / std::max(1.0e-20f, sigma2));
    
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

StreamlineStopReason StreamlineComputer::evalDirectionAt(const Vec3& x,
                                                         Vec3& dir,
                                                         float& hsml) const {
  float buf[3] = {0.0f, 0.0f, 0.0f};
  if (!evalFieldAt(&x.x, buf, hsml)) {
    dir = {0.0f, 0.0f, 0.0f};
    return StreamlineStopReason::FieldEvalFailed;
  }

  const Vec3 B{buf[0], buf[1], buf[2]};
  const float b = len(B);
  if (b <= WeakFieldFloor) {
    dir = {0.0f, 0.0f, 0.0f};
    return StreamlineStopReason::WeakField;
  }

  dir = B / b;
  return StreamlineStopReason::None;
}

StreamlineStopReason StreamlineComputer::RK4stepArcLength(const Vec3& x,
                                                          float& h,
                                                          float sign,
                                                          float stepScale,
                                                          Vec3& x_next) const {
  Vec3 k1, k2, k3, k4;
  float hsml1 = 0.0f, hsml2 = 0.0f, hsml3 = 0.0f, hsml4 = 0.0f;

  StreamlineStopReason reason = evalDirectionAt(x, k1, hsml1);
  if (reason != StreamlineStopReason::None) return reason;

  const float h_abs_prev = std::abs(h);
  const float h_abs = clamp_positive(
      stepScale * hsml1,
      std::max(1.0e-6f, MinStepFrac * hsml1),
      std::max(2.0e-6f, MaxStepFrac * hsml1));

  const float ds = (h_abs_prev > 0.0f)
      ? std::copysign(std::min(std::max(h_abs_prev, 0.5f * h_abs), 2.0f * h_abs), sign)
      : std::copysign(h_abs, sign);

  reason = evalDirectionAt(x + k1 * (0.5f * ds), k2, hsml2);
  if (reason != StreamlineStopReason::None) return reason;
  reason = evalDirectionAt(x + k2 * (0.5f * ds), k3, hsml3);
  if (reason != StreamlineStopReason::None) return reason;
  reason = evalDirectionAt(x + k3 * ds, k4, hsml4);
  if (reason != StreamlineStopReason::None) return reason;

  x_next = x + (k1 + k2 * 2.0f + k3 * 2.0f + k4) * (ds / 6.0f);
  h = ds;
  return StreamlineStopReason::None;
}

StreamlineTrace StreamlineComputer::integrateStreamline(const Vec3& seed,
                                                        float h_init,
                                                        int maxSteps,
                                                        float sign,
                                                        float stepScale) const {
  StreamlineTrace trace;
  trace.points.reserve(std::min(maxSteps, 4096));

  Vec3 x = seed;
  float h = h_init;

  if (!is_inside_(x)) {
    trace.stopReason = StreamlineStopReason::SeedOutsideRegion;
    return trace;
  }

  for (int i = 0; i < maxSteps; ++i) {
    trace.points.push_back(x);
    
    Vec3 x_next;
    StreamlineStopReason reason =
      RK4stepArcLength(x, h, sign, stepScale, x_next);
    if (reason != StreamlineStopReason::None) {
      trace.stopReason = reason;
      return trace;
    }
    
    if (!is_inside_(x_next)) {
      trace.stopReason = StreamlineStopReason::OutOfBounds;
      return trace;
    }

    const Vec3 dx = x_next - x;
    if (len2(dx) <= 1.0e-24f) {
      trace.stopReason = StreamlineStopReason::ZeroStep;
      return trace;
    }

    x = x_next;
  }

  trace.stopReason = StreamlineStopReason::MaxStepsReached;
  return trace;
}

StreamlineTrace StreamlineComputer::integrateBiStreamline(const Vec3& seed,
                                                          float h_init,
                                                          int maxSteps,
                                                          float stepScale) const {
  auto backward = integrateStreamline(seed, h_init, maxSteps, -1.0f, stepScale);
  auto forward = integrateStreamline(seed, h_init, maxSteps, +1.0f, stepScale);

  StreamlineTrace trace;
  trace.stopReason = forward.stopReason != StreamlineStopReason::MaxStepsReached
    ? forward.stopReason
    : backward.stopReason;

  if (!backward.points.empty()) {
    std::reverse(backward.points.begin(), backward.points.end());
    trace.points.insert(trace.points.end(),
                        backward.points.begin(),
                        backward.points.end());
    if (!trace.points.empty()) trace.points.pop_back();
  }
  trace.points.insert(trace.points.end(), forward.points.begin(), forward.points.end());
  return trace;
}

bool StreamlineComputer::is_inside_(const Vec3& x) const {
  if (!fieldBounds_.valid) return false;
  if (x.x < fieldBounds_.min[0] || x.x > fieldBounds_.max[0]) return false;
  if (x.y < fieldBounds_.min[1] || x.y > fieldBounds_.max[1]) return false;
  if (x.z < fieldBounds_.min[2] || x.z > fieldBounds_.max[2]) return false;
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
