#include "FileIO/snapshot_prefetch_controller.h"

#include "core/PerfTimer.h"
#include "app/input_filter_config.h"

SnapshotPrefetchController::SnapshotPrefetchController(SnapshotSource& source,
                                                       SnapshotLoader& loader)
  : source_(source), loader_(loader) {}

bool SnapshotPrefetchController::loadNewSnapshot(int newFileIndex,
                                                 SnapshotReadResult& outResult,
                                                 const InputFilterConfig& filter) {
  TIME_FUNCTION();

  bool foundInCache = false;
  {
    std::lock_guard<std::mutex> lock(prefetch_.mutex);
    if (prefetch_.cache.pop(newFileIndex, outResult)) {
      foundInCache = true;
    }
  }

  if (!foundInCache) {
    if (!isLoading_) {
      if (!loadBatch(newFileIndex,
                     source_.batchSize,
                     source_.skipStep,
                     outResult,
                     filter)) {
        return false;
      }
      foundInCache = true;
    } else {
      return false;
    }
  }

  source_.currentFileIndex = newFileIndex;
  return foundInCache;
}

bool SnapshotPrefetchController::syncLoadFirstFile(int targetFile,
                                                   SnapshotReadResult& outResult,
                                                   const InputFilterConfig& filter) {
  if (!loader_.readFile(targetFile, outResult, filter)) {
    return false;
  }

  std::lock_guard<std::mutex> lock(prefetch_.mutex);
  prefetch_.cache.clear();

  return true;
}

void SnapshotPrefetchController::asyncLoadRemainingFiles(int targetFile,
                                                         int batchSize,
                                                         int skipStep,
                                                         int generation,
							 const InputFilterConfig& filter
							 ) {
  TrackingVector<SnapshotReadResult> loaded;

  for (int i = 1; i < batchSize; ++i) {
    int fileNumber = targetFile + i * skipStep;
    SnapshotReadResult result;
    
    if (loader_.readFile(fileNumber, result, filter)) {
      loaded.push_back(std::move(result));
    }
  }

  {
    std::lock_guard<std::mutex> lock(prefetch_.mutex);
    if (prefetch_.generation.load() != generation) return;

    for (auto& result : loaded) {
      prefetch_.cache.push(std::move(result));
    }
  }
}

bool SnapshotPrefetchController::loadBatch(int targetFile,
                                           int batchSize,
                                           int skipStep,
                                           SnapshotReadResult& outResult,
                                           const InputFilterConfig& filter) {
  const int gen = ++prefetch_.generation;
  prefetch_.running = true;
  isLoading_ = true;

  if (!syncLoadFirstFile(targetFile, outResult, filter)) {
    prefetch_.running = false;
    isLoading_ = false;
    return false;
  }

  prefetch_.future = std::async(std::launch::async,
                                [this, targetFile, batchSize, skipStep, gen, filter]() {
                                  asyncLoadRemainingFiles(targetFile, batchSize, skipStep, gen, filter);
                                  if (prefetch_.generation.load() == gen) {
                                    prefetch_.running = false;
                                    isLoading_ = false;
                                  }
                                });
  return true;
}
