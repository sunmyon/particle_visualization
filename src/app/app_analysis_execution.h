#pragma once

#include "app/app_state.h"

#ifdef GEOMETRICAL_ANALYSIS
class DiskRadiusFinder;
class ParticleArray;
class FileInfo;
struct RenderLayerState;
struct NormalizationContext;

void ExecuteSingleDiskAnalysisRequest(ParticleArray& particles,
				      NormalizationContext& normalization,
                                      DiskRadiusFinder& diskFinder,
                                      DiskAnalysisRequestState& request,
                                      DiskAnalysisResultState& result);

void ExecuteDiskBatchRequest(ParticleArray& particles,
			     NormalizationContext& normalization,
			     const InputFilterConfig& filter,
                             FileInfo& fileInfo,
                             DiskRadiusFinder& diskFinder,
                             const RenderLayerState& diskRenderState,
                             DiskAnalysisBatchRequestState& request,
                             DiskAnalysisBatchResultState& result);

void ExecuteSingleEllipsoidAnalysisRequest(ParticleArray& particles,
                                           EllipseFitter& ellipsoidFitter,
                                           EllipsoidAnalysisRequestState& request,
                                           EllipsoidAnalysisResultState& result);

void ExecuteEllipsoidBatchRequest(ParticleArray& particles,
				  NormalizationContext& normalization,
				  const InputFilterConfig& filter,
                                  FileInfo& fileInfo,
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

void ExecuteStellarDensityRequest(ParticleArray& particles,
				  const NormalizationContext& normalization,
                                  StellarDensityRequestState& request);

#ifdef CLUMP_DATA_READ
void ExecuteClumpBatchRequest(ParticleArray& particles,
			      NormalizationContext& normalization,
			      const InputFilterConfig& filter,
                              FileInfo& fileInfo,
                              FindClump& clumpFind,
                              ClumpBatchRequestState& request,
                              ClumpBatchResultState& result);
#endif

void ExecuteProjectionMovieRequest(ParticleArray& particles,
				   NormalizationContext& normalization,
				   const InputFilterConfig& filter,
                                   FileInfo& fileInfo,
                                   ProjectionMapGenerator& projectionMap,
                                   const CameraContext& camera,
                                   ProjectionMovieRequestState& request,
                                   ProjectionMovieResultState& result);

void ExecuteFileNavigationRequests(FileInfo& fileInfo,
				   ParticleArray& particles,
				   NormalizationContext& normalization,
				   const InputFilterConfig& filter,
				   FileNavigationRuntimeState& rt);

void ExecuteCameraPlacementRequests(ParticleArray& particles,
				    NormalizationContext& normalization,
				    CameraContext& camCtx,
				    SettingsRuntimeState& rt);

void ExecutePostSnapshotLoadActions(ParticleArray& particles,
				    NormalizationContext& normalization,
				    CameraContext& camCtx,
				    int currentFileIndex);
