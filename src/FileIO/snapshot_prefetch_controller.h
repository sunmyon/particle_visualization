#pragma once

#include "FileIO/file_prefetch_state.h"
#include "FileIO/snapshot_read_result.h"
#include "FileIO/snapshot_source.h"
#include "FileIO/snapshot_loader.h"

struct InputFilterConfig;

class SnapshotPrefetchController {
public:
  SnapshotPrefetchController(SnapshotSource& source, SnapshotLoader& loader);

  bool loadNewSnapshot(int newFileIndex,
                       SnapshotReadResult& outResult,
                       const InputFilterConfig& filter);

  bool isLoading() const {
    return isLoading_;
  }

private:
  bool syncLoadFirstFile(int targetFile,
                         SnapshotReadResult& outResult,
                         const InputFilterConfig& filter);
  void asyncLoadRemainingFiles(int targetFile, int batchSize, int skipStep, int generation, const InputFilterConfig& filter);
  bool loadBatch(int targetFile,
                 int batchSize,
                 int skipStep,
                 SnapshotReadResult& outResult,
                 const InputFilterConfig& filter);

private:
  SnapshotSource& source_;
  SnapshotLoader& loader_;
  PrefetchState prefetch_;
  bool isLoading_ = false;
};
