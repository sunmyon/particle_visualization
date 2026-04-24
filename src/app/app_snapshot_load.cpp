#include "app/app_snapshot_load.h"

#include "app/app_state.h"
#include "FileIO/file_io.h"

static void MarkPostSnapshotLoad(SnapshotPostprocessState& post)
{
  post.refreshTree = true;
  post.refreshCulling = true;
  post.refreshTopParticles = true;
  post.applyTrackingToCamera = true;
}

void ProcessSnapshotLoadQueue(AppDataState& data,
                              AppRuntimeState& runtime)
{
  runtime.snapshotLoad.result = SnapshotLoadResultState{};

  auto& req = runtime.snapshotLoad.request;
  if (!req.pending) return;
  if (data.fileInfo->isLoading()) return;

  auto& src = data.fileInfo->editSource();
  src.currentStep = req.targetStep;
  const int newFileIndex = src.initialIndex + src.currentStep * src.skipStep;
  src.currentFileIndex = newFileIndex;

  if (req.kind == SnapshotLoadKind::GenerateTestData) {
    data.fileInfo->generateTestData(data.particles,
                                    data.header,
                                    runtime.settings.normalization);
  } else {
    data.fileInfo->loadNewSnapshot(newFileIndex,
                                   data.particles,
                                   data.header,
                                   runtime.settings.normalization,
                                   runtime.settings.inputFilter);
  }

  runtime.snapshotLoad.result.loadedThisFrame = true;
  runtime.snapshotLoad.result.loadedStep = src.currentStep;
  runtime.snapshotLoad.result.owner = req.owner;

  if (req.owner == SnapshotLoadOwner::UserNavigation ||
      req.owner == SnapshotLoadOwner::ProjectionMovie) {
    MarkPostSnapshotLoad(runtime.settings.snapshotPostprocess);
  }

  req = SnapshotLoadRequestState{};
}
