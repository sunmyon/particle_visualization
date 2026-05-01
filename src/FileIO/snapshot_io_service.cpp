#include "FileIO/snapshot_io_service.h"

#include "app/state/input_filter_config.h"

SnapshotIOService::SnapshotIOService()
  : loader_(),
    prefetch_(loader_)
{
}

bool SnapshotIOService::loadNewSnapshot(int newFileIndex,
                                        const SnapshotLoadParams& params,
                                        SnapshotReadResult& outResult,
                                        const InputFilterConfig& filter)
{
  return prefetch_.loadNewSnapshot(newFileIndex, params, outResult, filter);
}

std::vector<int64_t> SnapshotIOService::getStarParticleID(int indexFile,
                                                          const SnapshotLoadParams& params,
                                                          const InputFilterConfig& filter)
{
  return loader_.getStarParticleID(indexFile, params, filter);
}

bool SnapshotIOService::isLoading() const
{
  return prefetch_.isLoading();
}
