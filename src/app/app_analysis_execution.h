#pragma once

class ParticleArray;
class FileInfo;
class HaloStore;
class ProjectionMapGenerator;
struct HeaderInfo;
struct RenderLayerState;
struct NormalizationContext;
struct ViewFilterConfig;
struct TrackingTargetState;
struct SnapshotSource;
struct HaloesUIState;
struct CameraContext;
struct ProjectionMapParams;
struct ProjectionMovieAnalysisRuntime;
struct SnapshotLoadRuntimeState;
struct SnapshotPostprocessState;

#ifdef USE_CONVEX_HULL
class FindClump;
class ConvexHullGenerator;
#endif

#ifdef GEOMETRICAL_ANALYSIS
class DiskRadiusFinder;

void ExecuteSingleDiskAnalysisRequest(ParticleArray& particles,
				      NormalizationContext& normalization,
                                      DiskRadiusFinder& diskFinder,
                                      DiskAnalysisRequestState& request,
                                      DiskAnalysisResultState& result);

void ExecuteDiskBatchRequest(ParticleArray& particles,
			     HeaderInfo& header,
			     NormalizationContext& normalization,
                             FileInfo& fileInfo,
                             SnapshotLoadRuntimeState& snapshotLoad,
                             DiskRadiusFinder& diskFinder,
                             const RenderLayerState& diskRenderState,
                             DiskAnalysisBatchRequestState& request,
                             DiskAnalysisBatchResultState& result);

void ExecuteSingleEllipsoidAnalysisRequest(ParticleArray& particles,
                                           EllipseFitter& ellipsoidFitter,
                                           EllipsoidAnalysisRequestState& request,
                                           EllipsoidAnalysisResultState& result);

void ExecuteEllipsoidBatchRequest(ParticleArray& particles,
                                  FileInfo& fileInfo,
                                  SnapshotLoadRuntimeState& snapshotLoad,
                                  EllipseFitter& ellipsoidFitter,
                                  EllipsoidAnalysisBatchRequestState& request,
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

#ifdef USE_CONVEX_HULL
struct ConvexHullRuntimeState;
void ExecuteConvexHullRequests(ParticleArray& particles,
                               FindClump& clumpFind,
                               ConvexHullGenerator& convexHull,
                               ConvexHullRuntimeState& convexState,
                               RenderLayerState& polyhedraState);
#endif

void ExecuteStellarDensityRequest(ParticleArray& particles,
				  const NormalizationContext& normalization,
                                  StellarDensityRequestState& request,
				  double time);

#ifdef CLUMP_DATA_READ
void ExecuteClumpBatchRequest(ParticleArray& particles,
			      HeaderInfo& header,
                              FileInfo& fileInfo,
                              SnapshotLoadRuntimeState& snapshotLoad,
                              FindClump& clumpFind,
                              ClumpBatchRequestState& request,
                              ClumpBatchResultState& result);
#endif

void ExecuteProjectionMovieRequest(ParticleArray& particles,
                                   HeaderInfo& header,
                                   NormalizationContext& normalization,
                                   TrackingTargetState& track,
                                   FileInfo& fileInfo,
                                   ProjectionMapGenerator& projectionMap,
                                   const ProjectionMapParams& baseParams,
                                   CameraContext& camera,
                                   ProjectionMovieAnalysisRuntime& movie,
                                   SnapshotLoadRuntimeState& snapshotLoad,
                                   SnapshotPostprocessState& post,
                                   ProjectionMovieResultState& result);

void ExecuteFileNavigationRequests(FileInfo& fileInfo,
                                   FileNavigationRuntimeState& rt,
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

void ExecuteHaloesUIRequests(HaloesUIState& state,
                             HaloStore& haloes,
                             ParticleArray& particles);

#ifdef PYTHON_BRIDGE
void ExecutePythonBridgeRequests(ParticleArray& particles,
                                 PythonBridgeState& service,
                                 PythonBridgeRequestState& request,
                                 PythonBridgeViewState& view);
#endif
