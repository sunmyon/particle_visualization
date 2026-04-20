#pragma once

#include "FileIO/file_prefetch_state.h"
#include "FileIO/snapshot_source.h"
#include "FileIO/snapshot_loader.h"

struct NormalizationContext;
struct InputFilterConfig;

class SnapshotPrefetchController {
public:
  SnapshotPrefetchController(SnapshotSource& source, SnapshotLoader& loader);

  void loadNewSnapshot(int newFileIndex, ParticleArray* P, NormalizationContext& normalization, const InputFilterConfig& filter);

  bool isLoading() const {
    return isLoading_;
  }

private:
  bool syncLoadFirstFile(int targetFile, ParticleArray* P, NormalizationContext& normalization, const InputFilterConfig& filter);
  void asyncLoadRemainingFiles(int targetFile, int batchSize, int skipStep, int generation, const InputFilterConfig& filter);
  void loadBatch(int targetFile, int batchSize, int skipStep, ParticleArray* P, NormalizationContext& normalization, const InputFilterConfig& filter);

private:
  SnapshotSource& source_;
  SnapshotLoader& loader_;
  PrefetchState prefetch_;
  bool isLoading_ = false;
};
