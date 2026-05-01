#pragma once
#include <algorithm>
#include <cstdint>
#include <type_traits>

#include "simulation_block.h"
#include <vector>

struct NormalizationContext;
struct QuantityState;
struct UnitSystem;
struct Header;

class SimulationDataset {
private:
  int simulationBlock_index; //current index in batch file list
  
public:
  SimulationDataset() = default;

  void rescalePositions(NormalizationContext& ctx);  
  
  bool particlesDirty = true;
  bool velocityDirty = true;
  bool flagParticleIndexDirty = true;
  
  SimulationBlock simulationBlock;
  std::vector<uint8_t> flag_mask;
  std::vector<uint8_t> flag_stress;

  void ensureParticleFlagStorage()
  {
    const size_t n = simulationBlock.particles.size();
    flag_mask.resize(n, 0);
    flag_stress.resize(n, 0);
  }

  void clearStressFlags()
  {
    ensureParticleFlagStorage();
    std::fill(flag_stress.begin(), flag_stress.end(), 0);
    particlesDirty = true;
  }

  template <class IdContainer>
  void ApplyIDStress(const IdContainer& ids)
  {
    ensureParticleFlagStorage();
    for (const auto& pid_raw : ids) {
      if constexpr (std::is_signed_v<std::decay_t<decltype(pid_raw)>>) {
        if (pid_raw < 0) continue;
      }
      const uint64_t pid = static_cast<uint64_t>(pid_raw);
      const size_t ip = simulationBlock.findIndexByID(pid);
      if (ip == static_cast<size_t>(-1)) continue;
      flag_stress[ip] = 1;
    }
    particlesDirty = true;
  }
  
  bool findParticleID(int64_t ID, float *pos)
  {
    if (ID < 0) return false;
    size_t ip = simulationBlock.findIndexByID(static_cast<uint64_t>(ID));
    if (ip == (size_t)-1) return false;
    
    const auto &p = simulationBlock.particles[ip];
    renderPosition(p, simulationBlock.worldToRenderScale, pos);
    return true;
  }
    
  bool setSimulationBlock(SimulationBlock&& newBlock, SimulationBlock* oldBlock, HeaderInfo& header, NormalizationContext& ctx, QuantityState& quantity);
  void computeStellarDensity(const std::array<bool,6>& selType,
                             bool flag_overwirte_hsml,
                             const NormalizationContext& ctx,
                             double time,
                             const UnitSystem& units);
};
