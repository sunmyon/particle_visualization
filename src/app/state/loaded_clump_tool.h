#pragma once

#include <vector>

class ClumpStore;
struct LoadedClumpWindowState;

class LoadedClumpTool {
public:
  struct ClumpEvolutionCache {
    std::vector<float> timeFloats;
    std::vector<float> valueFloats;
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

  const std::vector<ClumpEvolutionCache>& evolutionCache() const {
    return evolutionCache_;
  }

private:
  bool showEvolution_ = false;
  bool needCacheUpdate_ = false;

  std::vector<ClumpEvolutionCache> evolutionCache_;
};
