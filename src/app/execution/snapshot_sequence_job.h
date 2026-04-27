#pragma once

#include "app/state/runtime_state.h"
#include "app/state/snapshot_state_sync.h"

#include <cstddef>
#include <cstdio>

enum class SnapshotSequenceLoadState {
  Ready,
  Waiting,
  Failed
};

inline void BeginSnapshotSequenceJob(SnapshotJobRuntimeState& job,
                                     const FileNavigationRuntimeState& fileNav,
                                     int nSnapshots)
{
  job = SnapshotJobRuntimeState{};
  job.status = JobStatus::Running;
  job.savedCurrentStep = fileNav.navigation.currentStep;
  job.beginStep = fileNav.navigation.currentStep;
  job.endStep = fileNav.navigation.currentStep + nSnapshots - 1;
  job.nextStep = job.beginStep;
  job.stepStride = 1;
  job.processed = 0;
}

inline int SnapshotFileIndexForStep(const FileNavigationRuntimeState& fileNav,
                                    int targetStep)
{
  const auto& nav = fileNav.navigation;
  return nav.initialIndex + targetStep * nav.skipStep;
}

inline SnapshotSequenceLoadState EnsureSnapshotSequenceStepLoaded(
  SnapshotLoadRuntimeState& snapshotLoad,
  SnapshotLoadOwner owner,
  int targetStep,
  int priority,
  char* errorMessage,
  size_t errorMessageSize)
{
  if (IsSnapshotLoadedFor(snapshotLoad, owner, targetStep)) {
    return SnapshotSequenceLoadState::Ready;
  }

  if (IsSnapshotLoadFailedFor(snapshotLoad, owner, targetStep)) {
    if (errorMessage && errorMessageSize > 0) {
      std::snprintf(errorMessage,
                    errorMessageSize,
                    "%s",
                    snapshotLoad.result.errorMessage);
    }
    return SnapshotSequenceLoadState::Failed;
  }

  RequestSnapshotLoad(snapshotLoad, owner, targetStep, priority);
  return SnapshotSequenceLoadState::Waiting;
}

inline void AdvanceSnapshotSequenceJob(SnapshotJobRuntimeState& job,
                                       SnapshotLoadRuntimeState& snapshotLoad,
                                       SnapshotLoadOwner owner,
                                       int priority)
{
  ++job.processed;
  job.nextStep += job.stepStride;

  if (job.nextStep > job.endStep) {
    job.status = JobStatus::Completed;
    return;
  }

  RequestSnapshotLoad(snapshotLoad, owner, job.nextStep, priority);
}

inline void RestoreSnapshotSequenceNavigation(FileNavigationRuntimeState& fileNav,
                                              SnapshotLoadRuntimeState& snapshotLoad,
                                              const SnapshotJobRuntimeState& job,
                                              int priority)
{
  fileNav.navigation.currentStep = job.savedCurrentStep;
  RecomputeCurrentFileIndex(fileNav);
  RequestSnapshotLoad(snapshotLoad,
                      SnapshotLoadOwner::UserNavigation,
                      fileNav.navigation.currentStep,
                      priority);
}
