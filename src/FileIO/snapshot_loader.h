#pragma once

#include "FileIO/snapshot_source.h"
#include "FileIO/snapshot_read_result.h"
#include "data/particle_array.h"

struct NormalizationContext;
struct InputFilterConfig;

class SnapshotLoader {
public:
  explicit SnapshotLoader(SnapshotSource& source);

  bool readFile(int fileNumber,
                SnapshotReadResult& outResult,
                const InputFilterConfig& filter);

  TrackingVector<int> getStarParticleID(int indexFile, const InputFilterConfig& filter);
  void generateTestData(ParticleArray* P,
                        HeaderInfo& header,
                        NormalizationContext& normalization,
                        QuantityState& quantity);

private:
  SnapshotSource& source_;
};
