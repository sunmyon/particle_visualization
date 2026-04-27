#pragma once

#include "app/state/analysis_state.h"
#include "app/state/runtime_state.h"
#include "projection/projection_frame_execution.h"
#include "projection/projection_map_params.h"

struct ProjectionMapRequestState;
struct ProjectionMapToolState;
struct ProjectionPreviewDerivedState;
struct CameraContext;
struct RenderLayerState;
struct SnapshotPostprocessState;
struct TrackingTargetState;

struct ProjectionMapExecutionContext {
  ProjectionFrameExecutionContext& projection;
  const CameraContext& camera;
  int currentFileIndex = -1;
  double time = 0.0;

  ProjectionMapToolState* tool = nullptr;
  RenderLayerState* cuboidAnnotation = nullptr;
  ProjectionPreviewDerivedState* preview = nullptr;
};

ProjectionFrameResult ExecuteProjectionMapRequests(ProjectionMapRequestState& request,
                                                   ProjectionMapExecutionContext& ctx);

struct ProjectionMovieExecutionContext {
  ProjectionFrameExecutionContext& projection;
  TrackingTargetState& track;
  FileNavigationRuntimeState& fileNav;
  const ProjectionMapParams& baseParams;
  CameraContext& camera;
  ProjectionMovieRequestState& request;
  ProjectionMovieRuntimeState& runtime;
  SnapshotLoadRuntimeState& snapshotLoad;
  SnapshotPostprocessState& post;
  ProjectionMovieResultState& result;
};

void ExecuteProjectionMovieRequest(ProjectionMovieExecutionContext& ctx);
