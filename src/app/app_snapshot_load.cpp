#include "app/app_snapshot_load.h"

#include "app/app_state.h"
#include "app/snapshot_state_sync.h"
#include "app/app_services.h"
#include "FileIO/snapshot_io_service.h"
#include "data/particle_block.h"
#include "data/header_info.h"

#include <cstring>

static void MarkPostSnapshotLoad(SnapshotPostprocessState& post)
{
  post.refreshTree = true;
  post.refreshCulling = true;
  post.refreshTopParticles = true;
  post.applyTrackingToCamera = true;
}

static void CopyCStr(char* dst, std::size_t dstSize, const char* src)
{
  if (!dst || dstSize == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  std::strncpy(dst, src, dstSize);
  dst[dstSize - 1] = '\0';
}

static SnapshotLoadParams BuildSnapshotLoadParams(const AppDataState& data,
                                                  const AppRuntimeState& runtime)
{
  SnapshotLoadParams p;
  const auto& nav = runtime.settings.fileNavigation.navigation;
  const auto& input = runtime.settings.fileNavigation.input;
  const auto& fmt = runtime.settings.snapshotFormat;

  p.initialIndex = nav.initialIndex;
  p.currentFileIndex = nav.currentFileIndex;
  p.batchSize = nav.batchSize;
  p.skipStep = nav.skipStep;
  p.currentStep = nav.currentStep;

  CopyCStr(p.fileFormat, sizeof(p.fileFormat), input.fileFormat);
  CopyCStr(p.folderPath, sizeof(p.folderPath), input.folderPath);
  CopyCStr(p.filePath, sizeof(p.filePath), input.filePath);
#ifdef HAVE_HDF5
  p.useHDF5 = input.useHDF5;
#endif

  p.readFormat = fmt.readFormat;
  p.formatTokens = fmt.formatTokens;
  p.formatTokensHdf5 = fmt.formatTokensHdf5;
  p.units = data.quantity.units;
  return p;
}

static void GenerateTestDataSnapshot(AppDataState& data,
                                     AppRuntimeState& runtime)
{
  data.header = HeaderInfo{};
  ParticleBlock block = ParticleBlock::makeTestParticleBlock(data.header);
  ParticleBlock oldBlock;
  data.particles->setParticleBlock(std::move(block),
                                   &oldBlock,
                                   data.header,
                                   runtime.settings.normalization,
				   data.quantity);
}

void ProcessSnapshotLoadQueue(AppDataState& data,
                              AppRuntimeState& runtime,
                              AppServices& services)
{
  runtime.snapshotLoad.result = SnapshotLoadResultState{};

  auto& req = runtime.snapshotLoad.request;
  if (!req.pending) return;
  if (!services.snapshotIO) return;
  if (services.snapshotIO->isLoading()) return;

  auto& fileNav = runtime.settings.fileNavigation;
  auto& nav = fileNav.navigation;
  nav.currentStep = req.targetStep;
  RecomputeCurrentFileIndex(fileNav);
  const int newFileIndex = nav.currentFileIndex;

  if (req.kind == SnapshotLoadKind::GenerateTestData) {
    GenerateTestDataSnapshot(data, runtime);
  } else {
    const SnapshotLoadParams params = BuildSnapshotLoadParams(data, runtime);
    SnapshotReadResult loaded;
    const bool ok = services.snapshotIO->loadNewSnapshot(newFileIndex,
                                                         params,
                                                         loaded,
                                                         runtime.settings.inputFilter);
    if (!ok) {
      req = SnapshotLoadRequestState{};
      return;
    }
    data.header = loaded.header;
    ParticleBlock oldBlock;
    data.particles->setParticleBlock(std::move(loaded.block),
                                     &oldBlock,
                                     data.header,
                                     runtime.settings.normalization,
				     data.quantity);
  }

  fileNav.current.loadedFileIndex = nav.currentFileIndex;
  fileNav.current.loadedTime = data.header.time;

  runtime.snapshotLoad.result.loadedThisFrame = true;
  runtime.snapshotLoad.result.loadedStep = nav.currentStep;
  runtime.snapshotLoad.result.owner = req.owner;

  if (req.owner == SnapshotLoadOwner::UserNavigation ||
      req.owner == SnapshotLoadOwner::ProjectionMovie) {
    MarkPostSnapshotLoad(runtime.settings.snapshotPostprocess);
  }

  req = SnapshotLoadRequestState{};
}
