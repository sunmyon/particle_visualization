#pragma once

#include "FileIO/snapshot_source.h"
#include "data/particle_array.h"

struct NormalizationContext;
struct InputFilterConfig;
struct HeaderInfo;

class SnapshotLoader {
public:
  explicit SnapshotLoader(SnapshotSource& source);

  bool loadSingleFile(int fileNumber, ParticleBlock& outBlock, HeaderInfo& header, const InputFilterConfig& filter);
  bool loadFirstFileIntoArray(int targetFile, ParticleArray* P, HeaderInfo& header, NormalizationContext& normalization, const InputFilterConfig& filter);

  TrackingVector<int> getStarParticleID(int indexFile, const InputFilterConfig& filter);
  void generateTestData(ParticleArray* P, HeaderInfo& header, NormalizationContext& normalization);

private:
  SnapshotSource& source_;
};
