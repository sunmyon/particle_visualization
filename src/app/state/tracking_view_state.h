#pragma once
#include <array>
#include <cstdint>

enum class AngularMomentumViewMode {
  FaceOn = 0,
  EdgeOn = 1
};

struct TrackingTargetState {
  bool followParticle = false;
  int64_t targetParticleID = -1;

  bool followClump = false;
  int targetClumpID = -1;

  bool followSinkParticle = false;
  bool followSinkParticleMostMassive = true;
  int64_t targetSinkParticleID = -1;

  bool useMassCenter = false;
  float massCenterRadius = 0.0f;       // <= 0 means unlimited.
  float massCenterMinDensity = 0.0f;   // Threshold for gas particles, type 0.

  bool alignToAngularMomentum = false;
  AngularMomentumViewMode amViewMode = AngularMomentumViewMode::FaceOn;
  float amRadius = 0.0f;               // <= 0 means unlimited.
  bool amSubtractBulkVelocity = true;
  std::array<bool,6> amUseType = {true, true, true, true, true, true};

  bool amKeepSignContinuity = true;
  bool amHasLastAxis = false;
  float amLastAxis[3] = {0.0f, 0.0f, 1.0f};

  bool renewAfterSnapshot = false;
};
