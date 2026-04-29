#pragma once

#include "app/state/analysis_state.h"
#include "app/state/runtime_state.h"

class ParticleArray;
class HaloStore;
class ClumpStore;
class EllipseFitter;
class ProjectionMapGenerator;
struct RenderLayerState;
struct RenderRuntimeState;
struct ParticleVisualConfig;
struct QuantityState;
struct UnitSystem;
struct SettingsRuntimeState;
struct NormalizationContext;
struct ViewFilterConfig;
struct TrackingTargetState;
struct HaloesUIState;
struct CameraContext;
struct ProjectionMapParams;
struct ProjectionMovieRuntimeState;
struct SnapshotLoadRuntimeState;
struct SnapshotPostprocessState;
struct PythonBridgeState;

#ifdef STREAM_LINE
class StreamlineComputer;
#endif

#ifdef USE_CONVEX_HULL
class FindClump;
#endif

#ifdef USE_CONVEX_HULL
class ConvexHullGenerator;
#endif

#ifdef GEOMETRICAL_ANALYSIS
class DiskRadiusFinder;

void ExecuteSingleDiskAnalysisRequest(ParticleArray& particles,
				      NormalizationContext& normalization,
                                      DiskRadiusFinder& diskFinder,
                                      DiskAnalysisRequestState& request,
                                      DiskAnalysisResultState& result,
				      const UnitSystem& units);

void ExecuteDiskBatchRequest(ParticleArray& particles,
			     NormalizationContext& normalization,
                             FileNavigationRuntimeState& fileNav,
                             SnapshotLoadRuntimeState& snapshotLoad,
                             DiskRadiusFinder& diskFinder,
                             const RenderLayerState& diskRenderState,
                             DiskAnalysisBatchRequestState& request,
                             DiskAnalysisBatchRuntimeState& runtime,
				     DiskAnalysisBatchResultState& result,
			     const UnitSystem& units);

void ExecuteSingleEllipsoidAnalysisRequest(ParticleArray& particles,
                                           EllipseFitter& ellipsoidFitter,
                                           EllipsoidAnalysisRequestState& request,
                                           EllipsoidAnalysisResultState& result);

void ExecuteEllipsoidBatchRequest(ParticleArray& particles,
                                  FileNavigationRuntimeState& fileNav,
	                                  SnapshotLoadRuntimeState& snapshotLoad,
	                                  EllipseFitter& ellipsoidFitter,
	                                  EllipsoidAnalysisBatchRequestState& request,
	                                  EllipsoidAnalysisBatchRuntimeState& runtime,
	                                  EllipsoidAnalysisBatchResultState& result);
#endif


#ifdef STREAM_LINE
void ExecuteStreamlinePreviewRequest(StreamlinePreviewRequestState& request,
                                     StreamlinePreviewResultState& result);

void ExecuteStreamlineBuildRequest(ParticleArray& particles,
                                   StreamlineComputer& streamLine,
                                   StreamlineBuildRequestState& request,
                                   StreamlineBuildResultState& result);
#endif

#ifdef ISO_CONTOUR
void ExecuteIsoContourRequest(ParticleArray& particles,
                              IsoContourRequestState& request,
                              IsoContourGeometryState& geometry,
                              RenderLayerState& isoContourRenderState);
#endif

#ifdef VOLUME_RENDERING
void ExecuteVolumeRenderingRequest(ParticleArray& particles,
                                   VolumeRenderingRequestState& request,
                                   VolumeRenderingResultState& result,
                                   VolumeRenderState& volumeRenderState);
#endif

#ifdef USE_CONVEX_HULL
struct ConvexHullRuntimeState;
void ExecuteConvexHullRequests(ParticleArray& particles,
                               FindClump& clumpFind,
                               ConvexHullGenerator& convexHull,
                               ConvexHullRuntimeState& convexState,
                               RenderLayerState& polyhedraState);
#endif

void ExecuteStellarDensityRequest(ParticleArray& particles,
				  const UnitSystem& units,
				  const NormalizationContext& normalization,
                                  StellarDensityRequestState& request,
				  double time);

void ExecuteSettingsActionRequests(ParticleArray& particles,
                                   QuantityState& quantity,
                                   ParticleVisualConfig& particleVisual,
                                   RenderRuntimeState& render,
                                   SettingsRuntimeState& settings,
                                   SnapshotPostprocessState& post);

void ExecuteFileNavigationRequests(FileNavigationRuntimeState& rt,
                                   SnapshotLoadRuntimeState& snapshotLoad);

void ExecuteCameraPlacementRequests(ParticleArray& particles,
				    const NormalizationContext& normalization,
				    ViewFilterConfig& viewFilter,
				    CameraContext& camCtx,
				    SettingsRuntimeState& rt,
				   SnapshotPostprocessState &post);

void ExecutePostSnapshotLoadActions(ParticleArray& particles,
				    ClumpStore& clumpStore,
				    NormalizationContext& normalization,
				    TrackingTargetState& track,
				    CameraContext& camCtx,
				    SnapshotPostprocessState &post,
				    int currentFileIndex);

#ifdef PYTHON_BRIDGE
void ExecutePythonBridgeRequests(ParticleArray& particles,
                                 PythonBridgeState& service,
                                 PythonBridgeRequestState& request,
                                 PythonBridgeViewState& view);
#endif
