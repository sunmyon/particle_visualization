#pragma once

#include "app/state/analysis_state.h"
#include "app/state/runtime_state.h"

class SimulationDataset;
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

void ExecuteSingleDiskAnalysisRequest(SimulationDataset& particles,
				      NormalizationContext& normalization,
                                      DiskRadiusFinder& diskFinder,
                                      DiskAnalysisRequestState& request,
                                      DiskAnalysisResultState& result,
				      const UnitSystem& units);

void ExecuteDiskBatchRequest(SimulationDataset& particles,
			     NormalizationContext& normalization,
                             FileNavigationRuntimeState& fileNav,
                             SnapshotLoadRuntimeState& snapshotLoad,
                             DiskRadiusFinder& diskFinder,
                             const RenderLayerState& diskRenderState,
                             DiskAnalysisBatchRequestState& request,
                             DiskAnalysisBatchRuntimeState& runtime,
				     DiskAnalysisBatchResultState& result,
			     const UnitSystem& units);

void ExecuteSingleEllipsoidAnalysisRequest(SimulationDataset& particles,
                                           EllipseFitter& ellipsoidFitter,
                                           EllipsoidAnalysisRequestState& request,
                                           EllipsoidAnalysisResultState& result);

void ExecuteEllipsoidBatchRequest(SimulationDataset& particles,
                                  FileNavigationRuntimeState& fileNav,
	                                  SnapshotLoadRuntimeState& snapshotLoad,
	                                  EllipseFitter& ellipsoidFitter,
	                                  EllipsoidAnalysisBatchRequestState& request,
	                                  EllipsoidAnalysisBatchRuntimeState& runtime,
	                                  EllipsoidAnalysisBatchResultState& result);
#endif


#ifdef STREAM_LINE
void ExecuteStreamlinePreviewRequest(const SimulationDataset& particles,
                                     StreamlinePreviewRequestState& request,
                                     StreamlinePreviewResultState& result);

void ExecuteStreamlineBuildRequest(SimulationDataset& particles,
                                   StreamlineComputer& streamLine,
                                   StreamlineBuildRequestState& request,
                                   StreamlineBuildResultState& result);
#endif

#ifdef ISO_CONTOUR
void ExecuteIsoContourRequest(SimulationDataset& particles,
                              IsoContourRequestState& request,
                              IsoContourGeometryState& geometry,
                              RenderLayerState& isoContourRenderState);
#endif

#ifdef VOLUME_RENDERING
void ExecuteVolumeRenderingRequest(SimulationDataset& particles,
                                   VolumeRenderingRequestState& request,
                                   VolumeRenderingResultState& result,
                                   VolumeRenderState& volumeRenderState);
#endif

#ifdef USE_CONVEX_HULL
struct ConvexHullRuntimeState;
void ExecuteConvexHullRequests(SimulationDataset& particles,
                               FindClump& clumpFind,
                               ConvexHullGenerator& convexHull,
                               ConvexHullRuntimeState& convexState,
                               RenderLayerState& polyhedraState);
#endif

void ExecuteStellarDensityRequest(SimulationDataset& particles,
				  const UnitSystem& units,
				  const NormalizationContext& normalization,
                                  StellarDensityRequestState& request,
				  double time);

#ifdef POWER_SPECTRUM
void ExecutePowerSpectrumRequest(SimulationDataset& particles,
                                 PowerSpectrumRequestState& request,
                                 PowerSpectrumResultState& result);
#endif

void ExecuteSettingsActionRequests(SimulationDataset& particles,
                                   QuantityState& quantity,
                                   ParticleVisualConfig& particleVisual,
                                   RenderRuntimeState& render,
                                   SettingsRuntimeState& settings,
                                   SnapshotPostprocessState& post);

void ExecuteFileNavigationRequests(FileNavigationRuntimeState& rt,
                                   SnapshotLoadRuntimeState& snapshotLoad);

void ExecuteCameraPlacementRequests(SimulationDataset& particles,
				    const NormalizationContext& normalization,
				    ViewFilterConfig& viewFilter,
				    CameraContext& camCtx,
				    SettingsRuntimeState& rt,
				   SnapshotPostprocessState &post);

void ExecutePostSnapshotLoadActions(SimulationDataset& particles,
				    ClumpStore& clumpStore,
				    NormalizationContext& normalization,
				    TrackingTargetState& track,
				    CameraContext& camCtx,
				    SnapshotPostprocessState &post,
				    int currentFileIndex);

#ifdef PYTHON_BRIDGE
void ExecutePythonBridgeRequests(SimulationDataset& particles,
                                 PythonBridgeState& service,
                                 PythonBridgeRequestState& request,
                                 PythonBridgeViewState& view);
#endif
