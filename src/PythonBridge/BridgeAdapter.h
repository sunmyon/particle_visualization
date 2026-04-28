#pragma once
#include <vector>
#include "PythonBridge/PythonBridge.h"  // PythonBridge::Shared, FieldId
#include "data/particle_array.h"

namespace bridge {
  // Initial bulk copy from AoS to SHM.
  bool loadInitialFromAoS(PythonBridge& bridge, const ParticleArray& P);
  
  // Apply Python edits from SHM to AoS. An empty dirty list means full sync.
  void applyFromSharedToAoS(const PythonBridge::Shared& S, ParticleArray& P,
			    const std::vector<FieldId>& dirty = {});

} // namespace bridge
