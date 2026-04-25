#pragma once
#include "particle_block.h"
#include "core/tracking_vector.h"

struct NormalizationContext;
struct QuantityState;
struct UnitSystem;
struct Header;

class ParticleArray {
private:
  int particleBlock_index; //current index in batch file list
  
public:
  ParticleArray() = default;

  void rescalePositions(NormalizationContext& ctx);  
  
  bool particlesDirty = true;
  bool velocityDirty = true;
  bool flagParticleIndexDirty = true;
  
  ParticleBlock particleBlock;
  TrackingVector<uint8_t> flag_mask;

  template <class IdContainer>
  void ApplyIDStress(const IdContainer& ids)
  {
    for (const auto& pid_raw : ids) {
      const uint64_t pid = static_cast<uint64_t>(pid_raw);
      const size_t ip = particleBlock.findIndexByID(pid);
      if (ip == static_cast<size_t>(-1)) continue;
      particleBlock.particles[ip].flag_stress = 1;
    }
    particlesDirty = true;
  }
  
  bool findParticleID(int ID, float *pos)
  {
    size_t ip = particleBlock.findIndexByID((uint64_t)(int64_t)ID);
    if (ip == (size_t)-1) return false;
    
    const auto &p = particleBlock.particles[ip];
    pos[0] = p.pos[0]; pos[1] = p.pos[1]; pos[2] = p.pos[2];
    return true;
  }
    
  bool setParticleBlock(ParticleBlock&& newBlock, ParticleBlock* oldBlock, HeaderInfo& header, NormalizationContext& ctx, QuantityState& quantity);
  void computeStellarDensity(const std::array<bool,6>& selType,
                             bool flag_overwirte_hsml,
                             const NormalizationContext& ctx,
                             double time,
                             const UnitSystem& units);
};
