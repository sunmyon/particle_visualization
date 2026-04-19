#include "data/particle_array.h"
#include "core/PerfTimer.h"

bool ParticleArray::setParticleBlock(ParticleBlock&& newBlock, ParticleBlock* oldBlock) {
  TIME_FUNCTION();

  bool hadOld = !particleBlock.particles.empty();
  if (oldBlock && hadOld) {
    *oldBlock = std::move(particleBlock);
  }
  particleBlock = std::move(newBlock);

  auto stats = particleBlock.rebuild(desiredMax);

  for (int q = 0; q < particleBlock.nUIQ; ++q) {
    for (int t = 0; t < kNumTypes; ++t) {
      particleValueMin[q][t] = stats.valueMin[q][t];
      particleValueMax[q][t] = stats.valueMax[q][t];
    }
  }

  originalMax = stats.originalMax;

  if (particleBlock.header.flag_hdf5) {
    units.length_cm = particleBlock.header.UnitLength_in_cm;
    units.mass_g = particleBlock.header.UnitMass_in_g;
    units.velocity_cm_per_s = particleBlock.header.UnitVelocity_in_cm_per_s;
    units.hubble = particleBlock.header.HubbleParam;
    units.useComovingCoordinate = particleBlock.header.flag_comoving;
    units.updateDerived();
  }

  flag_mask.resize(particleBlock.particles.size(), 0);
  
  particleBlock_index = 0; // あるいは廃止
  particlesDirty = true;
  flagParticleIndexDirty = true;

  return hadOld;
}
