#include "FileIO/snapshot_io_service.h"

#include <cstring>

#include "app/input_filter_config.h"

namespace {
void CopyCStr(char* dst, std::size_t dstSize, const char* src)
{
  if (!dst || dstSize == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  std::strncpy(dst, src, dstSize);
  dst[dstSize - 1] = '\0';
}
}

SnapshotIOService::SnapshotIOService()
  : source_(),
    loader_(source_),
    prefetch_(source_, loader_)
{
}

void SnapshotIOService::applyParams(const SnapshotLoadParams& params)
{
  source_.initialIndex = params.initialIndex;
  source_.currentFileIndex = params.currentFileIndex;
  source_.batchSize = params.batchSize;
  source_.skipStep = (params.skipStep > 0) ? params.skipStep : 1;
  source_.currentStep = params.currentStep;

  CopyCStr(source_.fileFormat, sizeof(source_.fileFormat), params.fileFormat);
  CopyCStr(source_.folderPath, sizeof(source_.folderPath), params.folderPath);
  CopyCStr(source_.filePath, sizeof(source_.filePath), params.filePath);

#ifdef HAVE_HDF5
  source_.useHDF5 = params.useHDF5;
#endif

  source_.setFormatMode(params.readFormat);
  source_.formatTokens = params.formatTokens;
  source_.formatTokens_hdf5 = params.formatTokensHdf5;
  source_.setUnit(params.units);
}

bool SnapshotIOService::loadNewSnapshot(int newFileIndex,
                                        const SnapshotLoadParams& params,
                                        SnapshotReadResult& outResult,
                                        const InputFilterConfig& filter)
{
  applyParams(params);
  return prefetch_.loadNewSnapshot(newFileIndex, outResult, filter);
}

TrackingVector<int> SnapshotIOService::getStarParticleID(int indexFile,
                                                         const SnapshotLoadParams& params,
                                                         const InputFilterConfig& filter)
{
  applyParams(params);
  return loader_.getStarParticleID(indexFile, filter);
}

bool SnapshotIOService::isLoading() const
{
  return prefetch_.isLoading();
}

