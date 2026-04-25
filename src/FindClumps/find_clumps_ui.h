#pragma once

#include "core/tracking_vector.h"
#include "data/particle_data.h"

class FindClump;
class LoadedClumpTool;
class ClumpChain;
class ProjectionMapGenerator;
class ParticleArray;
class ClumpStore;
struct HeaderInfo;
struct ProjectionMapParams;
struct SnapshotNavigationState;
struct SnapshotInputState;
struct SnapshotCurrentState;
struct CameraContext;
struct NormalizationContext;
struct TrackingTargetState;
struct SnapshotLoadRuntimeState;
struct UnitSystem;

struct ClumpFinderWindowState;
struct LoadedClumpWindowState;
struct ClumpChainWindowState;

void OpenClumpFindUI(ClumpFinderWindowState& state);
void OpenClumpListUI(LoadedClumpWindowState& state);
void OpenClumpChainUI(ClumpChainWindowState& state);

void DrawClumpFinderUI(ClumpFinderWindowState& ui,
		       FindClump& cfind,
		       TrackingVector<ParticleData>& originalParticles,
		       const HeaderInfo& header,
		       const SnapshotInputState& input,
                       const SnapshotCurrentState& current,
		       CameraContext& cam);

void DrawClumpListUI(LoadedClumpWindowState& ui,
		     LoadedClumpTool& ctool,
		     ClumpStore& clump,
		     TrackingTargetState& view,
		     int currentFileIndex,
		     const SnapshotInputState& input,
		     CameraContext& cam,
		     const NormalizationContext& normalization);

void DrawClumpChainListUI(ClumpChainWindowState& ui,
			  ClumpChain& chain,
			  ParticleArray* P,
			  const UnitSystem& units,
			  ProjectionMapGenerator* proj,
			  const ProjectionMapParams& baseParams,
			  const SnapshotNavigationState& nav,
                          const SnapshotCurrentState& current,
			  SnapshotLoadRuntimeState& snapshotLoad,
			  CameraContext& cam,
			  NormalizationContext& normalization);
