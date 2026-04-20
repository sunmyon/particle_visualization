#pragma once

#include "FileIO/snapshot_source.h"
#include "data/particle_array.h"

struct NormalizationContext;
class SnapshotLoader {
public:
  explicit SnapshotLoader(SnapshotSource& source);

  bool loadSingleFile(int fileNumber, ParticleBlock& outBlock);
  bool loadFirstFileIntoArray(int targetFile, ParticleArray* P, NormalizationContext& normalization);

  TrackingVector<int> getStarParticleID(int indexFile);
  void generateTestData(ParticleArray* P, NormalizationContext& normalization);

private:
  SnapshotSource& source_;
};
