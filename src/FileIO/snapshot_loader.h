#pragma once

#include "FileIO/snapshot_load_params.h"
#include "FileIO/snapshot_read_result.h"
#include "data/simulation_dataset.h"

struct NormalizationContext;
struct InputFilterConfig;

class SnapshotLoader {
public:
  SnapshotLoader() = default;

  bool readFile(int fileNumber,
                const SnapshotLoadParams& params,
                SnapshotReadResult& outResult,
                const InputFilterConfig& filter);

  std::vector<int64_t> getStarParticleID(int indexFile,
                                         const SnapshotLoadParams& params,
                                         const InputFilterConfig& filter);
  void generateTestData(SimulationDataset* P,
                        HeaderInfo& header,
                        NormalizationContext& normalization,
                        QuantityState& quantity);
};
