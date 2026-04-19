#pragma once

#include "FileIO/file_prefetch_state.h"
#include "FileIO/snapshot_source.h"
#include "FileIO/snapshot_loader.h"

class SnapshotPrefetchController {
public:
  SnapshotPrefetchController(SnapshotSource& source, SnapshotLoader& loader);

  void loadNewSnapshot(int newFileIndex, ParticleArray* P);

  bool isLoading() const {
    return isLoading_;
  }

private:
  bool syncLoadFirstFile(int targetFile, ParticleArray* P);
  void asyncLoadRemainingFiles(int targetFile, int batchSize, int skipStep, int generation);
  void loadBatch(int targetFile, int batchSize, int skipStep, ParticleArray* P);

private:
  SnapshotSource& source_;
  SnapshotLoader& loader_;
  PrefetchState prefetch_;
  bool isLoading_ = false;
};
