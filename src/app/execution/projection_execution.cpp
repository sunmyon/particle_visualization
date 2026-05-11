#include "app/execution/projection_execution.h"

#include "app/state/analysis_state.h"
#include "app/state/render_runtime_state.h"
#include "app/execution/snapshot_sequence_job.h"
#include "app/state/tracking_view_state.h"
#include "data/simulation_dataset.h"
#include "interaction/camera.h"
#include "interaction/interaction_utils.h"
#include "platform/shell_utils.h"
#include "projection/projection_map_context.h"
#include "projection/projection_map_tool_state.h"
#include "projection/projection_geometry.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

static float ProjectionWorldToRenderScale(const ProjectionMapExecutionContext& ctx)
{
  return ctx.projection.particles.simulationBlock.worldToRenderScale;
}

static glm::vec3 ProjectionDataToRender(const glm::vec3& p,
                                        float worldToRenderScale)
{
  return p * worldToRenderScale;
}

static glm::vec3 ProjectionRenderToData(const glm::vec3& p,
                                        float worldToRenderScale)
{
  return p / worldToRenderScale;
}

static void StoreQuatAsEulerDegrees(const glm::quat& q, float outEuler[3])
{
  const glm::vec3 euler = glm::degrees(glm::eulerAngles(glm::normalize(q)));
  outEuler[0] = euler.x;
  outEuler[1] = euler.y;
  outEuler[2] = euler.z;
}

static void SyncInteractiveCuboidFromParams(ProjectionMapToolState& tool)
{
  ProjectionEnsureLayoutInitialized(tool.params);
  const ProjectionViewBlockSpec block =
    ProjectionResolveViewBlock(tool.params, tool.params.activeViewBlockIndex);
  tool.interactiveCuboid.center = glm::vec3(block.xoffset[0],
                                            block.xoffset[1],
                                            block.xoffset[2]);
  tool.interactiveCuboid.halfSize = 0.5f * glm::vec3(block.xlen[0],
                                                     block.xlen[1],
                                                     block.xlen[2]);
  tool.interactiveCuboid.orientation =
    BuildProjectionTransformFromEuler(block.tilt);
  tool.appliedSelectedAxis = block.selectedAxis;
}

static void SyncParamsFromInteractiveCuboid(ProjectionMapToolState& tool)
{
  ProjectionEnsureLayoutInitialized(tool.params);
  ProjectionViewBlockSpec& main = tool.params.viewBlocks[0];
  ProjectionViewBlockSpec& block =
    tool.params.viewBlocks[tool.params.activeViewBlockIndex];
  ProjectionViewBlockSpec& centerTarget =
    (tool.params.activeViewBlockIndex != 0 && block.centerSameAsMain)
      ? main
      : block;
  centerTarget.xoffset[0] = tool.interactiveCuboid.center.x;
  centerTarget.xoffset[1] = tool.interactiveCuboid.center.y;
  centerTarget.xoffset[2] = tool.interactiveCuboid.center.z;

  ProjectionViewBlockSpec& tiltTarget =
    (tool.params.activeViewBlockIndex != 0 && block.tiltSameAsMain)
      ? main
      : block;
  StoreQuatAsEulerDegrees(tool.interactiveCuboid.orientation, tiltTarget.tilt);
  ProjectionSyncTopLevelFromViewBlock(tool.params,
                                      tool.params.activeViewBlockIndex);
}

static void MarkProjectionToolChanged(ProjectionMapToolState& tool,
                                      RenderLayerState& cuboidAnnotation)
{
  ++tool.revision;
  cuboidAnnotation.show = tool.params.flagShowCuboid;
  cuboidAnnotation.cpuUpdated = true;
}

static void ApplyProjectionMapResultToPreview(ProjectionFrameResult& result,
                                              ProjectionPreviewDerivedState& preview)
{
  if (!result.ok && !result.error.empty()) {
    std::cerr << result.error << "\n";
  }

  preview.image = std::move(result.image);
  preview.valid = preview.image.valid();
  if (preview.valid) {
    preview.version += 1;
    preview.image.version = preview.version;
    preview.computed = true;
  } else {
    preview.computed = false;
  }
}

ProjectionFrameResult ExecuteProjectionMapRequests(ProjectionMapRequestState& request,
                                                   ProjectionMapExecutionContext& ctx)
{
  ProjectionMapParams params = request.params;
  ProjectionMapToolState* tool = ctx.tool;
  RenderLayerState* cuboidAnnotation = ctx.cuboidAnnotation;
  const float worldToRenderScale = ProjectionWorldToRenderScale(ctx);

  if (request.paramsChanged) {
    ProjectionEnsureLayoutInitialized(request.params);
    ProjectionSyncTopLevelFromViewBlock(request.params,
                                        request.params.activeViewBlockIndex);
    if (tool) {
      tool->params = request.params;
      SyncInteractiveCuboidFromParams(*tool);
      if (cuboidAnnotation) {
        MarkProjectionToolChanged(*tool, *cuboidAnnotation);
      }
    }
    request.paramsChanged = false;
  } else if (tool) {
    params = tool->params;
  }

  if (request.moveCenterToCameraRequested) {
    if (tool) {
      ProjectionEnsureLayoutInitialized(tool->params);
      ProjectionViewBlockSpec& main = tool->params.viewBlocks[0];
      ProjectionViewBlockSpec& block =
        tool->params.viewBlocks[tool->params.activeViewBlockIndex];
      ProjectionViewBlockSpec& target =
        (tool->params.activeViewBlockIndex != 0 && block.centerSameAsMain)
          ? main
          : block;
      const glm::vec3 dataTarget =
        ProjectionRenderToData(ctx.camera.cameraTarget, worldToRenderScale);
      target.xoffset[0] = dataTarget.x;
      target.xoffset[1] = dataTarget.y;
      target.xoffset[2] = dataTarget.z;
      ProjectionSyncTopLevelFromViewBlock(tool->params,
                                          tool->params.activeViewBlockIndex);
      SyncInteractiveCuboidFromParams(*tool);
      if (cuboidAnnotation) {
        MarkProjectionToolChanged(*tool, *cuboidAnnotation);
      }
      params = tool->params;
    } else {
      ProjectionEnsureLayoutInitialized(params);
      ProjectionViewBlockSpec& main = params.viewBlocks[0];
      ProjectionViewBlockSpec& block =
        params.viewBlocks[params.activeViewBlockIndex];
      ProjectionViewBlockSpec& target =
        (params.activeViewBlockIndex != 0 && block.centerSameAsMain)
          ? main
          : block;
      const glm::vec3 dataTarget =
        ProjectionRenderToData(ctx.camera.cameraTarget, worldToRenderScale);
      target.xoffset[0] = dataTarget.x;
      target.xoffset[1] = dataTarget.y;
      target.xoffset[2] = dataTarget.z;
      ProjectionSyncTopLevelFromViewBlock(params, params.activeViewBlockIndex);
      params.xoffset[0] = dataTarget.x;
      params.xoffset[1] = dataTarget.y;
      params.xoffset[2] = dataTarget.z;
    }
    request.moveCenterToCameraRequested = false;
  }

  if (request.setAxisFromAngularMomentumRequested) {
    const ProjectionViewBlockSpec activeBlock =
      ProjectionResolveViewBlock(params, params.activeViewBlockIndex);
    ProjectionAngularMomentumFrame frame =
      ComputeAngularMomentumFrame(ctx.projection.particles.simulationBlock,
                                  glm::vec3(activeBlock.xoffset[0],
                                            activeBlock.xoffset[1],
                                            activeBlock.xoffset[2]),
                                  activeBlock.xlen);
    if (frame.valid) {
      ProjectionEnsureLayoutInitialized(params);
      ProjectionViewBlockSpec& main = params.viewBlocks[0];
      ProjectionViewBlockSpec& block =
        params.viewBlocks[params.activeViewBlockIndex];
      ProjectionViewBlockSpec& centerTarget =
        (params.activeViewBlockIndex != 0 && block.centerSameAsMain)
          ? main
          : block;
      centerTarget.xoffset[0] = frame.center.x;
      centerTarget.xoffset[1] = frame.center.y;
      centerTarget.xoffset[2] = frame.center.z;
      ProjectionViewBlockSpec& tiltTarget =
        (params.activeViewBlockIndex != 0 && block.tiltSameAsMain)
          ? main
          : block;
      StoreQuatAsEulerDegrees(BuildRotationFromZAxisTo(frame.axis), tiltTarget.tilt);
      ProjectionSyncTopLevelFromViewBlock(params, params.activeViewBlockIndex);
      if (tool) {
        tool->params = params;
        SyncInteractiveCuboidFromParams(*tool);
        if (cuboidAnnotation) {
          MarkProjectionToolChanged(*tool, *cuboidAnnotation);
        }
      }
    }
    request.setAxisFromAngularMomentumRequested = false;
  }

  if (request.arcballDragRequested) {
    if (tool) {
      const glm::mat4 view =
        glm::lookAt(ctx.camera.cameraPos, ctx.camera.cameraTarget, ctx.camera.cameraUp);
      CuboidObject renderCuboid = tool->interactiveCuboid;
      renderCuboid.center =
        ProjectionDataToRender(renderCuboid.center, worldToRenderScale);
      renderCuboid.halfSize =
        ProjectionDataToRender(renderCuboid.halfSize, worldToRenderScale);
      UpdateCuboidTransformArcball(renderCuboid,
                                   request.dragOldX,
                                   request.dragOldY,
                                   request.dragNewX,
                                   request.dragNewY,
                                   request.displayWidth,
                                   request.displayHeight,
                                   view,
                                   renderCuboid.center);
      tool->interactiveCuboid = renderCuboid;
      tool->interactiveCuboid.center =
        ProjectionRenderToData(renderCuboid.center, worldToRenderScale);
      tool->interactiveCuboid.halfSize =
        ProjectionRenderToData(renderCuboid.halfSize, worldToRenderScale);
      SyncParamsFromInteractiveCuboid(*tool);
      if (cuboidAnnotation) {
        MarkProjectionToolChanged(*tool, *cuboidAnnotation);
      }
      params = tool->params;
    }
    request.arcballDragRequested = false;
  }

  if (!request.renderRequested) return ProjectionFrameResult{};

  std::string warning;
  std::string outputPath =
    ResolveProjectionMapOutputPath(params,
                                   ctx.currentFileIndex,
                                   &warning);
  ProjectionFrameResult result =
    ExecuteProjectionFrame(ctx.projection,
                           params,
                           ctx.time,
                           ProjectionFrameOutputOptions{std::move(outputPath),
                                                        true,
                                                        ctx.preview != nullptr});
  result.warning = warning;
  if (!warning.empty()) {
    std::cerr << warning << "\n";
  }

  if (ctx.preview) {
    ApplyProjectionMapResultToPreview(result, *ctx.preview);
  }
  
  request.renderRequested = false;
  return result;
}

static void SaveCameraToJob(const CameraContext& camera, SnapshotJobRuntimeState& job)
{
  job.savedCameraValid = true;
  job.savedCameraPos = {
    camera.cameraPos.x, camera.cameraPos.y, camera.cameraPos.z
  };
  job.savedCameraTarget = {
    camera.cameraTarget.x, camera.cameraTarget.y, camera.cameraTarget.z
  };
  job.savedCameraUp = {
    camera.cameraUp.x, camera.cameraUp.y, camera.cameraUp.z
  };
#ifdef ROTATE_QUATERNION
  job.savedCameraOrientation = {
    camera.cameraOrientation.w,
    camera.cameraOrientation.x,
    camera.cameraOrientation.y,
    camera.cameraOrientation.z
  };
#endif
  job.savedCameraDistance = camera.distance;
}

static void RestoreCameraFromJob(CameraContext& camera, const SnapshotJobRuntimeState& job)
{
  if (!job.savedCameraValid) return;

  camera.cameraPos = glm::vec3(job.savedCameraPos[0], job.savedCameraPos[1], job.savedCameraPos[2]);
  camera.cameraTarget = glm::vec3(job.savedCameraTarget[0], job.savedCameraTarget[1], job.savedCameraTarget[2]);
  camera.cameraUp = glm::vec3(job.savedCameraUp[0], job.savedCameraUp[1], job.savedCameraUp[2]);
  camera.distance = job.savedCameraDistance;
#ifdef ROTATE_QUATERNION
  camera.cameraOrientation = glm::quat(job.savedCameraOrientation[0],
                                       job.savedCameraOrientation[1],
                                       job.savedCameraOrientation[2],
                                       job.savedCameraOrientation[3]);
#endif
}

static void ApplyMovieRequestToTracking(const ProjectionMovieRequestState& request,
                                        TrackingTargetState& track)
{
  track.followParticle = false;
  track.followClump = false;
  track.followSinkParticle = request.followSinkCenter;
  track.followSinkParticleMostMassive = request.followMostMassiveSink;
  track.targetSinkParticleID = request.particleIdCenter;
  track.useMassCenter = request.useMassCenter;
  track.massCenterRadius = request.massCenterRadius;
  track.massCenterMinDensity = request.massCenterMinDensity;

  track.alignToAngularMomentum = (request.alignToAngularMomentum || request.faceOn);
  track.amViewMode = request.faceOn ? AngularMomentumViewMode::FaceOn : request.amViewMode;
  track.amRadius = request.amRadius;
  track.amSubtractBulkVelocity = request.amSubtractBulkVelocity;
  track.amUseType = request.amUseType;
  track.amKeepSignContinuity = request.amKeepSignContinuity;
  track.amHasLastAxis = false;
  track.renewAfterSnapshot = true;
}

static void ApplyMovieCameraTargetToProjectionParams(ProjectionMapParams& params,
                                                     const glm::vec3& target)
{
  ProjectionEnsureLayoutInitialized(params);
  params.viewBlocks[0].xoffset[0] = target.x;
  params.viewBlocks[0].xoffset[1] = target.y;
  params.viewBlocks[0].xoffset[2] = target.z;
  params.xoffset[0] = target.x;
  params.xoffset[1] = target.y;
  params.xoffset[2] = target.z;
}

static bool PrepareProjectionMovieExecution(ProjectionMovieExecutionContext& ctx,
                                            const std::filesystem::path& framesDir)
{
  auto& job = ctx.runtime.job;

  if (ctx.request.cancelRequested) {
    job.cancelRequested = true;
    ctx.request.cancelRequested = false;
  }

  if (ctx.request.runRequested && job.status != JobStatus::Running) {
    ctx.runtime.params = ctx.request;
    ctx.runtime.params.runRequested = false;
    ctx.runtime.params.cancelRequested = false;
    ctx.runtime.projectionParams = ctx.request.hasProjectionParams
      ? ctx.request.projectionParams
      : ctx.baseParams;
    ProjectionEnsureLayoutInitialized(ctx.runtime.projectionParams);
    std::snprintf(ctx.runtime.projectionParams.folderPath,
                  sizeof(ctx.runtime.projectionParams.folderPath),
                  "%s",
                  ctx.runtime.params.outputFolderPath);
    std::snprintf(ctx.runtime.projectionParams.fileFormat,
                  sizeof(ctx.runtime.projectionParams.fileFormat),
                  "%s",
                  ctx.runtime.params.outputFileFormat);
    ctx.request.runRequested = false;
    ctx.result = ProjectionMovieResultState{};

    if (ctx.runtime.params.nSnapshots <= 0) {
      std::snprintf(ctx.result.errorMessage,
                    sizeof(ctx.result.errorMessage),
                    "nSnapshots must be > 0");
      ctx.result.success = false;
      ctx.result.completed = true;
      return false;
    }

    try {
      std::filesystem::remove_all(framesDir);
      std::filesystem::create_directories(framesDir);
      std::filesystem::create_directories(ctx.runtime.params.outputFolderPath);
    } catch (const std::exception& e) {
      std::snprintf(ctx.result.errorMessage,
                    sizeof(ctx.result.errorMessage),
                    "%s",
                    e.what());
      ctx.result.success = false;
      ctx.result.completed = true;
      return false;
    }

    BeginSnapshotSequenceJob(job, ctx.fileNav, ctx.runtime.params.nSnapshots);

    if (ctx.runtime.params.restoreCameraOnFinish) {
      SaveCameraToJob(ctx.camera, job);
    }
    job.savedTracking = ctx.track;
    job.savedTrackingValid = true;
    ApplyMovieRequestToTracking(ctx.runtime.params, ctx.track);
    ctx.post.applyTrackingToCamera = true;
  }

  if (job.status != JobStatus::Running) {
    return false;
  }

  if (job.cancelRequested) {
    job.status = JobStatus::Cancelled;
    return false;
  }

  const int targetStep = job.nextStep;
  const SnapshotSequenceLoadState loadState =
    EnsureSnapshotSequenceStepLoaded(ctx.snapshotLoad,
                                     SnapshotLoadOwner::ProjectionMovie,
                                     targetStep,
                                     10,
                                     ctx.result.errorMessage,
                                     sizeof(ctx.result.errorMessage));
  if (loadState == SnapshotSequenceLoadState::Waiting) {
    return false;
  }
  if (loadState == SnapshotSequenceLoadState::Failed) {
    job.status = JobStatus::Error;
    return false;
  }
  return true;
}

static void ApplyProjectionMovieFrameResult(ProjectionMovieExecutionContext& ctx,
                                            const ProjectionFrameResult& frame,
                                            const std::string& outputWarning)
{
  auto& job = ctx.runtime.job;

  if (!outputWarning.empty()) {
    std::snprintf(ctx.result.errorMessage,
                  sizeof(ctx.result.errorMessage),
                  "Unsafe outputFileFormat for movie: %s",
                  ctx.runtime.params.outputFileFormat);
    job.status = JobStatus::Error;
    return;
  }

  if (!frame.ok) {
    std::snprintf(ctx.result.errorMessage,
                  sizeof(ctx.result.errorMessage),
                  "%s",
                  frame.error.c_str());
    job.status = JobStatus::Error;
    return;
  }

  char linkname[512];
  std::snprintf(linkname,
                sizeof(linkname),
                "ffmpeg_frames/frame_%04d.png",
                job.processed);
  std::filesystem::remove(linkname);
  std::filesystem::create_symlink(std::filesystem::absolute(frame.outputPath),
                                  linkname);

  ++ctx.result.processedSnapshots;
  AdvanceSnapshotSequenceJob(job,
                             ctx.snapshotLoad,
                             SnapshotLoadOwner::ProjectionMovie,
                             10);
}

static void SubmitProjectionMovieFrameRequest(ProjectionMovieExecutionContext& ctx,
                                              ProjectionMapRequestState& request)
{
  request.params = ctx.runtime.projectionParams;
  ApplyMovieCameraTargetToProjectionParams(request.params,
                                           ctx.camera.cameraTarget);
  request.paramsChanged = true;
  request.renderRequested = true;
}

static void FinishProjectionMovieExecution(ProjectionMovieExecutionContext& ctx,
                                           const std::filesystem::path& framesDir)
{
  auto& job = ctx.runtime.job;

  if (job.status == JobStatus::Completed) {
    if (ctx.result.processedSnapshots <= 0) {
      std::snprintf(ctx.result.errorMessage,
                    sizeof(ctx.result.errorMessage),
                    "No frames were generated");
      job.status = JobStatus::Error;
    } else {
      const std::string outputMoviePath =
        std::string(ctx.runtime.params.outputFolderPath) + "/" +
        std::string(ctx.runtime.params.outputMovieName);
      std::string ffmpegCommand =
        "ffmpeg -y -framerate 30 -i " +
        ShellQuote("ffmpeg_frames/frame_%04d.png") +
        " -vf " +
        ShellQuote("scale=ceil(iw/2)*2:ceil(ih/2)*2") +
        " -c:v libx264 -pix_fmt yuv420p " +
        ShellQuote(outputMoviePath);

      const int ffmpegExit = std::system(ffmpegCommand.c_str());
      if (ffmpegExit != 0) {
        std::snprintf(ctx.result.errorMessage,
                      sizeof(ctx.result.errorMessage),
                      "ffmpeg failed with exit code %d",
                      ffmpegExit);
        job.status = JobStatus::Error;
      } else {
        std::snprintf(ctx.result.outputMoviePath,
                      sizeof(ctx.result.outputMoviePath),
                      "%s/%s",
                      ctx.runtime.params.outputFolderPath,
                      ctx.runtime.params.outputMovieName);
        ctx.result.success = true;
        ctx.result.completed = true;
      }
    }
  }

  if (job.status == JobStatus::Cancelled ||
      job.status == JobStatus::Error ||
      job.status == JobStatus::Completed) {
    std::filesystem::remove_all(framesDir);

    if (job.savedTrackingValid) {
      ctx.track = job.savedTracking;
    }

    if (ctx.runtime.params.restoreCameraOnFinish) {
      RestoreCameraFromJob(ctx.camera, job);
    }

    RestoreSnapshotSequenceNavigation(ctx.fileNav, ctx.snapshotLoad, job, 50);

    if (job.status == JobStatus::Cancelled) {
      if (ctx.result.errorMessage[0] == '\0') {
        std::snprintf(ctx.result.errorMessage,
                      sizeof(ctx.result.errorMessage),
                      "Cancelled by user");
      }
      ctx.result.success = false;
      ctx.result.completed = true;
    } else if (job.status == JobStatus::Error) {
      ctx.result.success = false;
      ctx.result.completed = true;
    }

    job = SnapshotJobRuntimeState{};
  }
}

void ExecuteProjectionMovieRequest(ProjectionMovieExecutionContext& ctx)
{
  const std::filesystem::path framesDir = "ffmpeg_frames";

  const bool canExecuteFrame = PrepareProjectionMovieExecution(ctx, framesDir);

  if (canExecuteFrame) {
    try {
      const int targetStep = ctx.runtime.job.nextStep;
      const int snapshotFileIndex = SnapshotFileIndexForStep(ctx.fileNav, targetStep);

      ProjectionMapRequestState frameRequest;
      SubmitProjectionMovieFrameRequest(ctx, frameRequest);

      ProjectionMapExecutionContext frameCtx{
        ctx.projection,
        ctx.camera,
        snapshotFileIndex,
        ctx.fileNav.current.loadedTime,
        nullptr,
        nullptr,
        nullptr
      };
      ProjectionFrameResult frame =
        ExecuteProjectionMapRequests(frameRequest, frameCtx);
      ApplyProjectionMovieFrameResult(ctx, frame, frame.warning);
    } catch (const std::exception& e) {
      std::snprintf(ctx.result.errorMessage,
                    sizeof(ctx.result.errorMessage),
                    "%s",
                    e.what());
      ctx.runtime.job.status = JobStatus::Error;
    }
  }

  FinishProjectionMovieExecution(ctx, framesDir);
}
