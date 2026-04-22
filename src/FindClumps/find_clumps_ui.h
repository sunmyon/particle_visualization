#pragma once

#include "core/tracking_vector.h"
#include "data/particle_data.h"

class FindClump;
class LoadedClumpTool;
class ClumpChain;
class ProjectionMapGenerator;
class FileInfo;
class ParticleArray;
struct SnapshotSource;
struct CameraContext;
struct NormalizationContext;
struct InputFilterConfig;

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
		       const SnapshotSource& src,
		       CameraContext& cam);

void DrawClumpListUI(LoadedClumpWindowState& ui,
		     LoadedClumpTool& ctool,
		     ClumpStore& clump,
		     TrackingTargetState& view,
		     int currentFileIndex,
		     const SnapshotSource& src,
		     CameraContext& cam,
		     const NormalizationContext& normalization);

void DrawClumpChainListUI(ClumpChainWindowState& ui,
			  ClumpChain& chain,
			  ParticleArray* P,
			  ProjectionMapGenerator* proj,
			  FileInfo& fileinfo,
			  CameraContext& cam,
			  NormalizationContext& normalization,
			  const InputFilterConfig& filter);
