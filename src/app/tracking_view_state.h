#pragma once
struct TrackingTargetState {
  bool followParticle = false;
  int targetParticleID = -1;

  bool followClump = false;
  int targetClumpID = -1;

  bool renewAfterSnapshot = false;
};
