#include "FileIO/snapshot_prefetch_controller.h"

#include "core/PerfTimer.h"

SnapshotPrefetchController::SnapshotPrefetchController(SnapshotSource& source,
                                                       SnapshotLoader& loader)
  : source_(source), loader_(loader) {}

void SnapshotPrefetchController::loadNewSnapshot(int newFileIndex, ParticleArray* P) {
  TIME_FUNCTION();

  bool foundInCache = false;
  ParticleBlock block;
  {
    std::lock_guard<std::mutex> lock(prefetch_.mutex);
    if (prefetch_.cache.pop(newFileIndex, block)) {
      ParticleBlock oldBlock;
      P->setParticleBlock(std::move(block), &oldBlock);
      foundInCache = true;
    }
  }

  if (!foundInCache) {
    if (!isLoading_) {
      loadBatch(newFileIndex, source_.batchSize, source_.skipStep, P);
    }
  }

  source_.currentFileIndex = newFileIndex;

  printf("currentStep=%d newFileIndex=%d\n", source_.currentStep, newFileIndex);

#ifdef CLUMP_DATA_READ
  P->flag_renew_clumpList = true;
#endif
}

bool SnapshotPrefetchController::syncLoadFirstFile(int targetFile, ParticleArray* P) {
  if (!loader_.loadFirstFileIntoArray(targetFile, P)) {
    return false;
  }

  std::lock_guard<std::mutex> lock(prefetch_.mutex);
  prefetch_.cache.clear();

  return true;
}

void SnapshotPrefetchController::asyncLoadRemainingFiles(int targetFile,
                                                         int batchSize,
                                                         int skipStep,
                                                         int generation) {
  TrackingVector<std::pair<int, ParticleBlock>> loaded;

  for (int i = 1; i < batchSize; ++i) {
    int fileNumber = targetFile + i * skipStep;
    ParticleBlock block;

    if (loader_.loadSingleFile(fileNumber, block)) {
      loaded.push_back({fileNumber, std::move(block)});
    }
  }

  {
    std::lock_guard<std::mutex> lock(prefetch_.mutex);
    if (prefetch_.generation.load() != generation) return;

    for (auto& e : loaded) {
      prefetch_.cache.push(e.first, std::move(e.second));
    }
  }
}

void SnapshotPrefetchController::loadBatch(int targetFile,
                                           int batchSize,
                                           int skipStep,
                                           ParticleArray* P) {
  const int gen = ++prefetch_.generation;
  prefetch_.running = true;
  isLoading_ = true;

  if (!syncLoadFirstFile(targetFile, P)) {
    prefetch_.running = false;
    isLoading_ = false;
    return;
  }

  prefetch_.future = std::async(std::launch::async,
                                [this, targetFile, batchSize, skipStep, gen]() {
                                  asyncLoadRemainingFiles(targetFile, batchSize, skipStep, gen);
                                  if (prefetch_.generation.load() == gen) {
                                    prefetch_.running = false;
                                    isLoading_ = false;
                                  }
                                });
}
