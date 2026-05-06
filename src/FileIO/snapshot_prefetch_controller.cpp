#include "FileIO/snapshot_prefetch_controller.h"

#include "core/PerfTimer.h"
#include "app/state/input_filter_config.h"

namespace {
void HashCombine(std::size_t& seed, std::size_t value)
{
  seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

template <typename T>
void HashValue(std::size_t& seed, const T& value)
{
  HashCombine(seed, std::hash<T>{}(value));
}

void HashCString(std::size_t& seed, const char* value)
{
  HashValue(seed, std::string(value ? value : ""));
}

void HashFieldSpecs(std::size_t& seed, const std::vector<FieldSpec>& specs)
{
  HashValue(seed, specs.size());
  for (const FieldSpec& spec : specs) {
    HashValue(seed, static_cast<int>(spec.key));
    HashValue(seed, static_cast<int>(spec.type));
    HashValue(seed, spec.count);
    HashValue(seed, spec.sourceName);
  }
}

std::size_t MakeSnapshotLoadSignature(const SnapshotLoadParams& params,
                                      const InputFilterConfig& filter)
{
  std::size_t seed = 0;

  HashValue(seed, params.initialIndex);
  HashValue(seed, params.batchSize);
  HashValue(seed, params.skipStep);
  HashCString(seed, params.fileFormat);
  HashCString(seed, params.folderPath);
  HashCString(seed, params.filePath);
#ifdef HAVE_HDF5
  HashValue(seed, params.useHDF5);
#endif
  HashValue(seed, static_cast<int>(params.readFormat));
  HashFieldSpecs(seed, params.formatTokens);
  HashFieldSpecs(seed, params.formatTokensHdf5);
  HashFieldSpecs(seed, params.formatTokensGadget);
  HashValue(seed, static_cast<int>(params.inputDensityUnit));
  HashValue(seed, static_cast<int>(params.inputTemperatureUnit));
  HashValue(seed, static_cast<int>(params.inputMagneticFieldUnit));

  HashValue(seed, params.units.length_cm);
  HashValue(seed, params.units.mass_g);
  HashValue(seed, params.units.velocity_cm_per_s);
  HashValue(seed, params.units.hubble);
  HashValue(seed, params.units.useComovingCoordinate);

  HashValue(seed, filter.enabled);
  HashValue(seed, filter.mask.enableSphere);
  HashValue(seed, filter.mask.center[0]);
  HashValue(seed, filter.mask.center[1]);
  HashValue(seed, filter.mask.center[2]);
  HashValue(seed, filter.mask.radius);
  HashValue(seed, static_cast<int>(filter.mask.outsideMode));
  HashValue(seed, filter.mask.outsideStride);
  for (auto mode : filter.mask.typeMode) {
    HashValue(seed, static_cast<int>(mode));
  }
  HashValue(seed, filter.mask.enableMaxParticles);
  HashValue(seed, filter.mask.maxParticles);

  return seed;
}
}

SnapshotPrefetchController::SnapshotPrefetchController(SnapshotLoader& loader)
  : loader_(loader) {}

bool SnapshotPrefetchController::loadNewSnapshot(int newFileIndex,
                                                 const SnapshotLoadParams& params,
                                                 SnapshotReadResult& outResult,
                                                 const InputFilterConfig& filter) {
  TIME_FUNCTION();

  const std::size_t signature = MakeSnapshotLoadSignature(params, filter);

  bool foundInCache = false;
  {
    std::lock_guard<std::mutex> lock(prefetch_.mutex);
    if (!cacheSignatureValid_ || cacheSignature_ != signature) {
      prefetch_.cache.clear();
      cacheSignature_ = signature;
      cacheSignatureValid_ = true;
    }

    if (prefetch_.cache.pop(newFileIndex, outResult)) {
      foundInCache = true;
    }
  }

  if (!foundInCache) {
    if (!isLoading_) {
      if (!loadBatch(newFileIndex, params, signature, outResult, filter)) {
        return false;
      }
      foundInCache = true;
    } else {
      return false;
    }
  }

  return foundInCache;
}

bool SnapshotPrefetchController::syncLoadFirstFile(int targetFile,
                                                   const SnapshotLoadParams& params,
                                                   SnapshotReadResult& outResult,
                                                   const InputFilterConfig& filter) {
  if (!loader_.readFile(targetFile, params, outResult, filter)) {
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
                                                         std::size_t signature,
                                                         SnapshotLoadParams params,
                                                         InputFilterConfig filter) {
  std::vector<SnapshotReadResult> loaded;

  for (int i = 1; i < batchSize; ++i) {
    int fileNumber = targetFile + i * skipStep;
    SnapshotReadResult result;
    
    if (loader_.readFile(fileNumber, params, result, filter)) {
      loaded.push_back(std::move(result));
    }
  }

  {
    std::lock_guard<std::mutex> lock(prefetch_.mutex);
    if (prefetch_.generation.load() != generation) return;
    if (!cacheSignatureValid_ || cacheSignature_ != signature) return;

    for (auto& result : loaded) {
      prefetch_.cache.push(std::move(result));
    }
  }
}

bool SnapshotPrefetchController::loadBatch(int targetFile,
                                           const SnapshotLoadParams& params,
                                           std::size_t signature,
                                           SnapshotReadResult& outResult,
                                           const InputFilterConfig& filter) {
  const int gen = ++prefetch_.generation;
  prefetch_.running = true;
  isLoading_.store(true);

  if (!syncLoadFirstFile(targetFile, params, outResult, filter)) {
    prefetch_.running = false;
    isLoading_.store(false);
    return false;
  }

  const int batchSize = params.batchSize;
  const int skipStep = (params.skipStep > 0) ? params.skipStep : 1;
  prefetch_.future = std::async(std::launch::async,
                                [this, targetFile, batchSize, skipStep, gen, signature, params, filter]() {
                                  asyncLoadRemainingFiles(targetFile, batchSize, skipStep, gen, signature, params, filter);
                                  if (prefetch_.generation.load() == gen) {
                                    prefetch_.running = false;
                                    isLoading_.store(false);
                                  }
                                });
  return true;
}
