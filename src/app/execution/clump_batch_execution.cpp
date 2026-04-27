#include "app/execution/clump_batch_execution.h"

#ifdef CLUMP_DATA_READ

#include "FindClumps/find_clumps.h"
#include "FindClumps/find_clumps_helpers.h"
#include "app/state/analysis_state.h"
#include "app/execution/snapshot_sequence_job.h"
#include "app/state/runtime_state.h"
#include "data/particle_array.h"

#include <cstdio>
#include <string>

bool ExecuteClumpRequest(ParticleArray& particles,
                         FindClump& clumpFind,
                         ClumpRequestState& request)
{
  if (!request.runRequested) {
    return false;
  }

  request.runRequested = false;

  if (particles.particleBlock.particles.empty() ||
      request.outputPath[0] == '\0' ||
      request.snapshotIndex < 0) {
    return false;
  }

  clumpFind.do_FOF_and_output_clump_data(request.method,
                                         particles.particleBlock.particles,
                                         request.snapshotTime,
                                         request.outputPath,
                                         request.snapshotIndex);
  return true;
}

static bool PrepareClumpBatchExecution(FileNavigationRuntimeState& fileNav,
                                       SnapshotLoadRuntimeState& snapshotLoad,
                                       FindClump& clumpFind,
                                       ClumpBatchRequestState& request,
                                       ClumpBatchRuntimeState& runtime,
                                       ClumpBatchResultState& result)
{
  auto& nav = fileNav.navigation;
  auto& job = runtime.job;

  if (request.cancelRequested) {
    job.cancelRequested = true;
    request.cancelRequested = false;
  }

  if (request.runRequested && job.status != JobStatus::Running) {
    runtime.params = request;
    runtime.params.runRequested = false;
    runtime.params.cancelRequested = false;
    request.runRequested = false;
    result = ClumpBatchResultState{};

    char filename[512];
    std::snprintf(filename,
                  sizeof(filename),
                  "%s/%s",
                  runtime.params.outputFolderPath,
                  runtime.params.outputFileName);
    std::snprintf(result.outputPath, sizeof(result.outputPath), "%s", filename);

    if (runtime.params.nSnapshots <= 0) {
      std::snprintf(result.errorMessage,
                    sizeof(result.errorMessage),
                    "nSnapshots must be > 0");
      result.completed = true;
      return false;
    }

    if (nav.skipStep <= 0) {
      std::snprintf(result.errorMessage,
                    sizeof(result.errorMessage),
                    "invalid skipStep for clump batch: %d",
                    nav.skipStep);
      result.completed = true;
      return false;
    }

    clumpFind.initialize_prev_nodes();
    BeginSnapshotSequenceJob(job, fileNav, runtime.params.nSnapshots);
  }

  if (job.status != JobStatus::Running) return false;

  if (job.cancelRequested) {
    job.status = JobStatus::Cancelled;
    return false;
  }

  const int targetStep = job.nextStep;
  const SnapshotSequenceLoadState loadState =
    EnsureSnapshotSequenceStepLoaded(snapshotLoad,
                                     SnapshotLoadOwner::ClumpBatch,
                                     targetStep,
                                     20,
                                     result.errorMessage,
                                     sizeof(result.errorMessage));
  if (loadState == SnapshotSequenceLoadState::Waiting) {
    return false;
  }
  if (loadState == SnapshotSequenceLoadState::Failed) {
    job.status = JobStatus::Error;
    return false;
  }
  return true;
}

static void FinishClumpBatchExecution(FileNavigationRuntimeState& fileNav,
                                      SnapshotLoadRuntimeState& snapshotLoad,
                                      ClumpBatchRuntimeState& runtime,
                                      ClumpBatchResultState& result)
{
  auto& nav = fileNav.navigation;
  auto& job = runtime.job;

  if (job.status == JobStatus::Cancelled ||
      job.status == JobStatus::Error ||
      job.status == JobStatus::Completed) {
    RestoreSnapshotSequenceNavigation(fileNav, snapshotLoad, job, 50);

    if (job.status == JobStatus::Completed) {
      const int initstep = nav.currentFileIndex;
      const int dstep = nav.skipStep;
      give_stellar_id_to_clumps(initstep,
                                runtime.params.nSnapshots,
                                dstep,
                                std::string(result.outputPath));
      result.completed = true;
    } else {
      if (job.status == JobStatus::Cancelled) {
        std::snprintf(result.errorMessage,
                      sizeof(result.errorMessage),
                      "Cancelled by user");
      }
      result.completed = true;
    }

    runtime.job = SnapshotJobRuntimeState{};
  }
}

void ExecuteClumpBatchRequest(ParticleArray& particles,
                              FileNavigationRuntimeState& fileNav,
                              SnapshotLoadRuntimeState& snapshotLoad,
                              FindClump& clumpFind,
                              ClumpBatchRequestState& request,
                              ClumpBatchRuntimeState& runtime,
                              ClumpBatchResultState& result)
{
  const bool canExecuteFrame =
    PrepareClumpBatchExecution(fileNav,
                               snapshotLoad,
                               clumpFind,
                               request,
                               runtime,
                               result);

  if (canExecuteFrame) {
    const int targetStep = runtime.job.nextStep;
    const int snapshotFileIndex = SnapshotFileIndexForStep(fileNav, targetStep);

    ClumpRequestState frameRequest;
    frameRequest.runRequested = true;
    frameRequest.method = runtime.params.method;
    frameRequest.snapshotIndex = snapshotFileIndex;
    frameRequest.snapshotTime = fileNav.current.loadedTime;
    std::snprintf(frameRequest.outputPath,
                  sizeof(frameRequest.outputPath),
                  "%s",
                  result.outputPath);

    if (ExecuteClumpRequest(particles, clumpFind, frameRequest)) {
      result.processedSnapshots++;
      runtime.job.processed = result.processedSnapshots;
    }

    AdvanceSnapshotSequenceJob(runtime.job,
                               snapshotLoad,
                               SnapshotLoadOwner::ClumpBatch,
                               20);
  }

  FinishClumpBatchExecution(fileNav, snapshotLoad, runtime, result);
}

#endif
