#pragma once

#include <vector>

class ClumpChain;
class ClumpStore;
class FindClump;
class LoadedClumpTool;
class ParticleArray;
class ProjectionMapGenerator;
struct CameraContext;
struct ClumpChainWindowState;
struct ClumpFinderWindowState;
struct LoadedClumpWindowState;
struct NormalizationContext;
class ParticleData;
struct ProjectionMapParams;
struct SnapshotCurrentState;
struct SnapshotInputState;
struct SnapshotLoadRuntimeState;
struct TrackingTargetState;
struct UnitSystem;

void ExecuteClumpFinderWindowRequests(ClumpFinderWindowState& ui,
                                      FindClump& clumpFind,
                                      ParticleArray& particles,
                                      CameraContext& camera,
                                      double snapshotTime,
                                      const SnapshotInputState& input,
                                      const SnapshotCurrentState& current);

void ExecuteLoadedClumpWindowRequests(LoadedClumpWindowState& ui,
                                      LoadedClumpTool& clumpTool,
                                      ClumpStore& clumpStore,
                                      TrackingTargetState& tracking,
                                      CameraContext& camera,
                                      int currentFileIndex,
                                      const SnapshotInputState& input,
                                      const NormalizationContext& normalization);

void ExecuteClumpChainWindowRequests(ClumpChainWindowState& ui,
                                     ClumpChain& chain,
                                     ParticleArray& particles,
                                     const UnitSystem& units,
                                     ProjectionMapGenerator& projectionMap,
                                     const ProjectionMapParams& baseParams,
                                     const SnapshotCurrentState& current,
                                     SnapshotLoadRuntimeState& snapshotLoad,
                                     CameraContext& cam,
                                     NormalizationContext& normalization);
