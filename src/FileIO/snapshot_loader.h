#pragma once

#include "FileIO/snapshot_source.h"
#include "data/particle_array.h"

class SnapshotLoader {
public:
  explicit SnapshotLoader(SnapshotSource& source);

  bool loadSingleFile(int fileNumber, ParticleBlock& outBlock);
  bool loadFirstFileIntoArray(int targetFile, ParticleArray* P);

  TrackingVector<int> getStarParticleID(int indexFile);
  void generateTestData(ParticleArray* P);

private:
  SnapshotSource& source_;
};
