#pragma once

struct AnalysisDerivedState;
struct CameraContext;
struct FileNavigationRuntimeState;
struct NormalizationContext;
struct ProjectionMapToolState;
struct QuantityState;
struct RenderRuntimeState;
struct UnitSystem;
struct SnapshotCurrentState;
struct SnapshotInputState;
struct SnapshotLoadRuntimeState;
struct SnapshotPostprocessState;
struct TrackingTargetState;
struct ToolWindowUIState;
class ClumpChain;
class FindClump;
class LoadedClumpTool;
class ParticleArray;
class ProjectionMapGenerator;
class ClumpStore;
class HaloStore;

struct ParticleToolExecutionInput {
  ParticleArray& particles;
  CameraContext& camera;
  TrackingTargetState& tracking;
  SnapshotPostprocessState& snapshotPostprocess;
  const QuantityState& quantity;
};

struct AnalysisToolExecutionInput {
  ParticleArray& particles;
  QuantityState& quantity;
  NormalizationContext& normalization;
  AnalysisDerivedState& analysis;
  CameraContext& camera;
};

struct ProjectionToolExecutionInput {
  ProjectionMapGenerator* projectionMap2D = nullptr;
  ClumpChain* clumpChain = nullptr;
  ParticleArray& particles;
  const UnitSystem& units;
  ProjectionMapToolState& projectionMap;
  const SnapshotCurrentState& snapshotCurrent;
  SnapshotLoadRuntimeState& snapshotLoad;
  CameraContext& camera;
  NormalizationContext& normalization;
};

struct HaloToolExecutionInput {
  HaloStore& haloStore;
  ParticleArray& particles;
  CameraContext& camera;
  const NormalizationContext& normalization;
};

struct ClumpToolExecutionInput {
  FindClump* clumpFind = nullptr;
  LoadedClumpTool* clumpLoad = nullptr;
  ClumpStore& clumpStore;
  ParticleArray& particles;
  TrackingTargetState& tracking;
  CameraContext& camera;
  const SnapshotInputState& snapshotInput;
  const SnapshotCurrentState& snapshotCurrent;
  NormalizationContext& normalization;
  int currentFileIndex = -1;
};

void ExecuteParticleToolRequests(ToolWindowUIState& tools,
                                 ParticleToolExecutionInput& input);

void ExecuteAnalysisToolRequests(ToolWindowUIState& tools,
                                 AnalysisToolExecutionInput& input);

void ExecuteProjectionToolRequests(ToolWindowUIState& tools,
                                   ProjectionToolExecutionInput& input);

void ExecuteDataFilterToolRequests(ToolWindowUIState& tools);

void ExecuteHaloToolRequests(ToolWindowUIState& tools,
                             HaloToolExecutionInput& input);

void ExecuteClumpToolRequests(ToolWindowUIState& tools,
                              ClumpToolExecutionInput& input);
