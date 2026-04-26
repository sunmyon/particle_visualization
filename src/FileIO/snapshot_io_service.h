#pragma once

#include "FileIO/snapshot_load_params.h"
#include "FileIO/snapshot_loader.h"
#include "FileIO/snapshot_prefetch_controller.h"
#include "FileIO/snapshot_read_result.h"

struct InputFilterConfig;

class SnapshotIOService {
public:
  SnapshotIOService();

  bool loadNewSnapshot(int newFileIndex,
                       const SnapshotLoadParams& params,
                       SnapshotReadResult& outResult,
                       const InputFilterConfig& filter);

  TrackingVector<int> getStarParticleID(int indexFile,
                                        const SnapshotLoadParams& params,
                                        const InputFilterConfig& filter);

  bool isLoading() const;

private:
  SnapshotLoader loader_;
  SnapshotPrefetchController prefetch_;
};
