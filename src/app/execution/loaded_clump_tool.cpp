#include "app/state/loaded_clump_tool.h"

#include "FileIO/clump_io.h"
#include "app/state/clump_window_state.h"
#include "data/clump_store.h"

#include <limits>
#include <string>

namespace {
const char* kEvolutionQuantities[] = {
  "Density", "Temperature", "ClumpMass", "StellarMass"
};
constexpr int kNumEvolutionQuantities =
  sizeof(kEvolutionQuantities) / sizeof(kEvolutionQuantities[0]);
}

void LoadedClumpTool::clearEvolutionCache()
{
  evolutionCache_.clear();
  needCacheUpdate_ = false;
}

void LoadedClumpTool::rebuildEvolutionCache(const LoadedClumpWindowState& ui,
                                            const ClumpStore& clumpStore,
                                            int currentFileIndex,
                                            float& outTMin,
                                            float& outTMax,
                                            float& outValMin,
                                            float& outValMax)
{
  evolutionCache_.clear();

  outTMin   =  std::numeric_limits<float>::infinity();
  outTMax   = -std::numeric_limits<float>::infinity();
  outValMin =  std::numeric_limits<float>::infinity();
  outValMax = -std::numeric_limits<float>::infinity();

#ifndef CLUMP_DATA_READ
  (void)ui;
  (void)clumpStore;
  (void)currentFileIndex;
  needCacheUpdate_ = false;
  return;
#else
  if (!clumpStore.loaded()) {
    needCacheUpdate_ = false;
    return;
  }

  int qidx = ui.selectedEvolutionVar;
  if (qidx < 0 || qidx >= kNumEvolutionQuantities) {
    needCacheUpdate_ = false;
    return;
  }

  const std::string var = kEvolutionQuantities[qidx];

  for (size_t i = 0; i < clumpStore.size(); ++i) {
    if (i >= ui.showEvolve.size() || !ui.showEvolve[i]) {
      continue;
    }

    const auto& ch = clumpStore.clump(static_cast<int>(i));

    TrackingVector<float> times;
    TrackingVector<ClumpData> clumps;

    readClumpEvolution(clumpStore.filePath(),
                       currentFileIndex,
                       ui.finalFileIndex,
                       ui.dsnapshot,
                       ch.clumpID,
                       times,
                       clumps);

    if (times.empty() || times.size() != clumps.size()) {
      continue;
    }

    ClumpEvolutionCache cache;
    cache.index = static_cast<int>(i);
    cache.timeFloats.resize(times.size());
    cache.valueFloats.resize(times.size());

    for (size_t j = 0; j < times.size(); ++j) {
      const float t = times[j];
      const float v = clumps[j].getValue(var);

      cache.timeFloats[j] = t;
      cache.valueFloats[j] = v;

      if (t < outTMin) outTMin = t;
      if (t > outTMax) outTMax = t;

      if (ui.useLogScaleY && v <= 0.0f) {
        continue;
      }

      if (v < outValMin) outValMin = v;
      if (v > outValMax) outValMax = v;
    }

    evolutionCache_.push_back(std::move(cache));
  }

  if (evolutionCache_.empty()) {
    outTMin = 0.0f;
    outTMax = 1.0f;
    outValMin = 0.0f;
    outValMax = 1.0f;
  } else {
    if (outTMin == outTMax) outTMax = outTMin + 1.0f;
    if (outValMin == outValMax) outValMax = outValMin + 1.0f;
  }

  needCacheUpdate_ = false;
#endif
}
