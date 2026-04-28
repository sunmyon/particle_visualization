#pragma once

#include "core/tracking_vector.h"

class ClumpStore;
struct LoadedClumpWindowState;

class LoadedClumpTool {
public:
  struct ClumpEvolutionCache {
    TrackingVector<float> timeFloats;
    TrackingVector<float> valueFloats;
    int index = -1;
  };

  void requestEvolutionPlotUpdate() {
    showEvolution_ = true;
    needCacheUpdate_ = true;
  }

  bool showEvolution() const { return showEvolution_; }
  bool needCacheUpdate() const { return needCacheUpdate_; }

  void clearEvolutionCache();

  void rebuildEvolutionCache(const LoadedClumpWindowState& ui,
                             const ClumpStore& clumpStore,
                             int currentFileIndex,
                             float& outTMin,
                             float& outTMax,
                             float& outValMin,
                             float& outValMax);

  const TrackingVector<ClumpEvolutionCache>& evolutionCache() const {
    return evolutionCache_;
  }

private:
  bool showEvolution_ = false;
  bool needCacheUpdate_ = false;

  TrackingVector<ClumpEvolutionCache> evolutionCache_;
};
