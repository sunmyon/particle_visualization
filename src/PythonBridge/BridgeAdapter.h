#pragma once
#include <vector>
#include "PythonBridge/PythonBridge.h"  // PythonBridge::Shared, FieldId
#include "data/simulation_dataset.h"

namespace bridge {
  // Initial bulk copy from AoS to SHM.
  bool loadInitialFromAoS(PythonBridge& bridge, const SimulationDataset& P);
  
  // Apply Python edits from SHM to AoS. An empty dirty list means full sync.
  void applyFromSharedToAoS(const PythonBridge::Shared& S, SimulationDataset& P,
			    const std::vector<FieldId>& dirty = {});

} // namespace bridge
