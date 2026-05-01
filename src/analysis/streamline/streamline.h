#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Dense>
#include <nanoflann.hpp>

#include <vector>
#include "data/simulation_block.h"

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
  enum class FieldSource {
    Velocity,
    BField
  };

  int nSeeds = 1;
  FieldSource fieldSource = FieldSource::Velocity;
  int maxSteps = 1000000;
  float stepScale = 0.15f;
  bool useManualSeed = false;
  std::vector<std::array<float, 3>> manualSeeds;
  StreamlineBoxSpec seedRegion;
  StreamlineBoxSpec fieldRegion;
  float thetaMaxDegrees = 10.0f;
};

enum class StreamlineStopReason {
  None,
  SeedOutsideRegion,
  FieldEvalFailed,
  WeakField,
  OutOfBounds,
  ZeroStep,
  MaxStepsReached
};

struct StreamlineSeedReport {
  int seedIndex = -1;
  float position[3] = {0.0f, 0.0f, 0.0f};
  StreamlineStopReason stopReason = StreamlineStopReason::None;
  int pointCount = 0;
  float length = 0.0f;
};

struct StreamlineBuildOutput {
  bool ok = false;
  std::string message;
  int seedCount = 0;
  int lineCount = 0;
  std::array<int, 7> stopCounts{};
  std::vector<StreamlineSeedReport> seedReports;
  std::vector<std::vector<Vec3>> lines;
};

struct StreamlineTrace {
  std::vector<Vec3> points;
  StreamlineStopReason stopReason = StreamlineStopReason::None;
};

class StreamlineComputer {
 public:
  StreamlineBuildOutput build(const SimulationBlock& particles,
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
    std::vector<particle_stream> particles;

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

  Bounds makeGasBounds(const std::vector<SimulationElement>& particles,
                       float worldToRenderScale) const;
  Bounds makeBoxBounds(const StreamlineBoxSpec& box) const;
  std::vector<SeedPoint> selectSeeds(const std::vector<SimulationElement>& particles,
                                     float worldToRenderScale,
                                     const Bounds& bounds,
                                     int nSeeds) const;
  SeedPoint makeManualSeed(const std::vector<SimulationElement>& particles,
                           float worldToRenderScale,
                           const std::array<float, 3>& seed) const;
  void estimate_gradB(const SimulationBlock& simulationBlock,
                      const Bounds& fieldBounds,
                      StreamlineBuildSpec::FieldSource fieldSource);
  bool evalFieldAt(const float x[3], float outB[3], float& hsml) const;
  StreamlineStopReason evalDirectionAt(const Vec3& x, Vec3& dir, float& hsml) const;

  StreamlineTrace integrateBiStreamline(const Vec3& seed,
                                        float h_init,
                                        int maxSteps,
                                        float stepScale) const;
  StreamlineTrace integrateStreamline(const Vec3& seed,
                                      float h_init,
                                      int maxSteps,
                                      float sign,
                                      float stepScale) const;
  std::vector<Vec3> sampleByCurvature(const std::vector<Vec3>& fullLine, float theta_max_rad) const;

  StreamlineStopReason RK4stepArcLength(const Vec3& x,
                                        float& h,
                                        float sign,
                                        float stepScale,
                                        Vec3& x_next) const;
  bool is_inside_(const Vec3& pos) const;

  static constexpr int   N_neighbours = 32;
  static constexpr float MinStepFrac = 0.03f;
  static constexpr float MaxStepFrac = 0.25f;
  static constexpr float WeakFieldFloor = 1.0e-20f;
  static constexpr float GradRegFraction = 1.0e-4f;
  static constexpr float LimiterEps = 1.0e-12f;

  std::vector<std::array<Eigen::Vector3f, 3>> gradB;
  std::vector<Eigen::Vector3f> gradLimiter;
  std::vector<float> r_neighbours;

  Bounds fieldBounds_;
};
