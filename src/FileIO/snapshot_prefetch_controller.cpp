#include "FileIO/snapshot_prefetch_controller.h"

#include "core/PerfTimer.h"
#include "app/input_filter_config.h"
#include "data/header_info.h"

SnapshotPrefetchController::SnapshotPrefetchController(SnapshotSource& source,
                                                       SnapshotLoader& loader)
  : source_(source), loader_(loader) {}

void SnapshotPrefetchController::loadNewSnapshot(int newFileIndex, ParticleArray* P, HeaderInfo& header, NormalizationContext& normalization, const InputFilterConfig& filter) {
  TIME_FUNCTION();

  bool foundInCache = false;
  ParticleBlock block;
  {
    std::lock_guard<std::mutex> lock(prefetch_.mutex);
    if (prefetch_.cache.pop(newFileIndex, block)) {
      ParticleBlock oldBlock;
      P->setParticleBlock(std::move(block), &oldBlock, header, normalization);
      foundInCache = true;
    }
  }

  if (!foundInCache) {
    if (!isLoading_) {
      loadBatch(newFileIndex, source_.batchSize, source_.skipStep, P, header, normalization, filter);
    }
  }

  source_.currentFileIndex = newFileIndex;

  printf("currentStep=%d newFileIndex=%d\n", source_.currentStep, newFileIndex);
}

bool SnapshotPrefetchController::syncLoadFirstFile(int targetFile, ParticleArray* P, HeaderInfo& header, NormalizationContext& normalization, const InputFilterConfig& filter) {
  if (!loader_.loadFirstFileIntoArray(targetFile, P, header, normalization, filter)) {
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
  TrackingVector<std::pair<int, ParticleBlock>> loaded;
  TrackingVector<std::pair<int, HeaderInfo>> loaded_header;

  for (int i = 1; i < batchSize; ++i) {
    int fileNumber = targetFile + i * skipStep;
    ParticleBlock block;
    HeaderInfo header;
    
    if (loader_.loadSingleFile(fileNumber, block, header, filter)) {
      loaded.push_back({fileNumber, std::move(block)});
      loaded_header.push_back({fileNumber, std::move(header)});
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
                                           ParticleArray* P,
					   HeaderInfo& header,
					   NormalizationContext& normalization,
					   const InputFilterConfig& filter) {
  const int gen = ++prefetch_.generation;
  prefetch_.running = true;
  isLoading_ = true;

  if (!syncLoadFirstFile(targetFile, P, header, normalization, filter)) {
    prefetch_.running = false;
    isLoading_ = false;
    return;
  }

  prefetch_.future = std::async(std::launch::async,
                                [this, targetFile, batchSize, skipStep, gen, filter]() {
                                  asyncLoadRemainingFiles(targetFile, batchSize, skipStep, gen, filter);
                                  if (prefetch_.generation.load() == gen) {
                                    prefetch_.running = false;
                                    isLoading_ = false;
                                  }
                                });
}
