#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Dense>
#include <nanoflann.hpp>

#include "core/tracking_vector.h"
#include "data/particle_block.h"

struct Vec3 {
  float x, y, z;

  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }

  Vec3& operator+=(const Vec3& o) {
    x += o.x; y += o.y; z += o.z;
    return *this;
  }
};

inline float dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float len2(const Vec3& a) {
  return dot(a, a);
}

inline float len(const Vec3& a) {
  return std::sqrt(len2(a));
}

inline Vec3 normalized(const Vec3& a) {
  const float l = len(a);
  if (l <= 0.0f) return {0.0f, 0.0f, 0.0f};
  return a / l;
}

struct StreamlineBoxSpec {
  bool enabled = false;
  float center[3] = {0.0f, 0.0f, 0.0f};
  float size[3] = {0.0f, 0.0f, 0.0f};
};

struct StreamlineBuildSpec {
  int nSeeds = 1;
  bool useManualSeed = false;
  float manualSeed[3] = {0.0f, 0.0f, 0.0f};
  StreamlineBoxSpec seedRegion;
  StreamlineBoxSpec fieldRegion;
  float thetaMaxDegrees = 10.0f;
};

class StreamlineComputer {
 public:
  std::vector<std::vector<Vec3>> build(const ParticleBlock& particles,
                                       const StreamlineBuildSpec& spec);

 private:
  struct Bounds {
    float min[3] = {0.0f, 0.0f, 0.0f};
    float max[3] = {0.0f, 0.0f, 0.0f};
    bool valid = false;
  };

  struct SeedPoint {
    Vec3 pos;
    float hsml = 0.0f;
  };

  struct particle_stream {
    float pos[3];
    float vect[3];
    float cell_size;
  };

  struct StreamParticleCloud {
    TrackingVector<particle_stream> particles;

    inline size_t kdtree_get_point_count() const { return particles.size(); }
    inline float kdtree_get_pt(const size_t idx, const size_t dim) const {
      return particles[idx].pos[dim];
    }
    template <class BBOX>
    bool kdtree_get_bbox(BBOX&) const { return false; }
  } cloud;

  using KDTreeType = nanoflann::KDTreeSingleIndexAdaptor<
      nanoflann::L2_Simple_Adaptor<float, StreamParticleCloud>,
      StreamParticleCloud,
      3>;

  using IndexType = KDTreeType::IndexType;
  using DistanceType = KDTreeType::DistanceType;

  std::unique_ptr<KDTreeType> m_kdTree;

  Bounds makeGasBounds(const TrackingVector<ParticleData>& particles) const;
  Bounds makeBoxBounds(const StreamlineBoxSpec& box) const;
  std::vector<SeedPoint> selectSeeds(const TrackingVector<ParticleData>& particles,
                                     const Bounds& bounds,
                                     int nSeeds) const;
  SeedPoint makeManualSeed(const TrackingVector<ParticleData>& particles,
                           const float seed[3]) const;
  void estimate_gradB(const ParticleBlock& particleBlock,
                      const Bounds& fieldBounds);
  bool evalFieldAt(const float x[3], float outB[3], float& hsml) const;
  bool evalDirectionAt(const Vec3& x, Vec3& dir, float& hsml) const;

  std::vector<Vec3> integrateBiStreamline(const Vec3& seed, float h_init, int maxstep) const;
  std::vector<Vec3> integrateStreamline(const Vec3& seed, float h_init, int maxstep, float sign) const;
  std::vector<Vec3> sampleByCurvature(const std::vector<Vec3>& fullLine, float theta_max_rad) const;

  bool RK4stepArcLength(const Vec3& x, float& h, float sign, Vec3& x_next) const;
  bool is_inside_(const Vec3& pos) const;

  static constexpr int   N_neighbours = 32;
  static constexpr int   MaxStep = 1000000;
  static constexpr float MinStepFrac = 0.03f;
  static constexpr float MaxStepFrac = 0.25f;
  static constexpr float WeakFieldFloor = 1.0e-20f;
  static constexpr float GradRegFraction = 1.0e-4f;
  static constexpr float LimiterEps = 1.0e-12f;

  TrackingVector<std::array<Eigen::Vector3f, 3>> gradB;
  TrackingVector<Eigen::Vector3f> gradLimiter;
  TrackingVector<float> r_neighbours;

  Bounds fieldBounds_;
};
