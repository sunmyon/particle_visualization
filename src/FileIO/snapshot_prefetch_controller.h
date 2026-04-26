#pragma once

#include "FileIO/file_prefetch_state.h"
#include "FileIO/snapshot_load_params.h"
#include "FileIO/snapshot_loader.h"
#include "FileIO/snapshot_read_result.h"

#include <cstddef>

struct InputFilterConfig;

class SnapshotPrefetchController {
public:
  explicit SnapshotPrefetchController(SnapshotLoader& loader);

  bool loadNewSnapshot(int newFileIndex,
                       const SnapshotLoadParams& params,
                       SnapshotReadResult& outResult,
                       const InputFilterConfig& filter);

  bool isLoading() const {
    return isLoading_.load();
  }

private:
  bool syncLoadFirstFile(int targetFile,
                         const SnapshotLoadParams& params,
                         SnapshotReadResult& outResult,
                         const InputFilterConfig& filter);
  void asyncLoadRemainingFiles(int targetFile,
                               int batchSize,
                               int skipStep,
                               int generation,
                               std::size_t signature,
                               SnapshotLoadParams params,
                               InputFilterConfig filter);
  bool loadBatch(int targetFile,
                 const SnapshotLoadParams& params,
                 std::size_t signature,
                 SnapshotReadResult& outResult,
                 const InputFilterConfig& filter);

private:
  SnapshotLoader& loader_;
  PrefetchState prefetch_;
  std::size_t cacheSignature_ = 0;
  bool cacheSignatureValid_ = false;
  std::atomic<bool> isLoading_{false};
};
