#include "app/execution/analysis_dispatch.h"

#include "app/state/app_state.h"
#include "app/execution/analysis_execution.h"
#include "app/execution/clump_batch_execution.h"
#include "app/execution/projection_execution.h"
#include "app/app_services.h"

void ExecuteAnalysisJobRequests(AppDataState& data,
                                AppRuntimeState& runtime,
                                AnalysisDerivedState& analysis,
                                AppServices& services,
                                CameraContext& camera,
                                int currentFileIndex)
{
#ifdef GEOMETRICAL_ANALYSIS
  ExecuteSingleDiskAnalysisRequest(*data.particles,
				   runtime.settings.normalization,
                                   *services.diskFinder,
                                   runtime.analysisRequests.disk,
                                   analysis.disk,
				   runtime.quantity.units);

  ExecuteDiskBatchRequest(*data.particles,
			  runtime.settings.normalization,
                          runtime.settings.fileNavigation,
                          runtime.snapshotLoad,
                          *services.diskFinder,
                          runtime.render.disks,
                          runtime.analysisRequests.diskBatch,
                          runtime.analysisJobs.diskBatch,
                          analysis.diskBatch,
			  runtime.quantity.units);

  ExecuteSingleEllipsoidAnalysisRequest(*data.particles,
                                        *services.ellipsoid,
                                        runtime.analysisRequests.ellipsoid,
                                        analysis.ellipsoid);

  ExecuteEllipsoidBatchRequest(*data.particles,
                               runtime.settings.fileNavigation,
                               runtime.snapshotLoad,
                               *services.ellipsoid,
                               runtime.analysisRequests.ellipsoidBatch,
                               runtime.analysisJobs.ellipsoidBatch,
                               analysis.ellipsoidBatch);
#endif

#ifdef STREAM_LINE
  ExecuteStreamlinePreviewRequest(runtime.analysisRequests.streamlinePreview,
				  analysis.streamlinePreview);

  ExecuteStreamlineBuildRequest(*data.particles,
				*services.streamLine,
				runtime.analysisRequests.streamlineBuild,
				analysis.streamlineBuild);
#endif

  ExecuteStellarDensityRequest(*data.particles,
			       runtime.quantity.units,
			       runtime.settings.normalization,
			       runtime.analysisRequests.stellarDensity,
			       runtime.settings.fileNavigation.current.loadedTime);

#ifdef ISO_CONTOUR
  ExecuteIsoContourRequest(*data.particles,
			   runtime.analysisRequests.isoContour,
			   analysis.isoContour,
			   runtime.render.isocontour);
#endif

#ifdef VOLUME_RENDERING
  ExecuteVolumeRenderingRequest(*data.particles,
                                runtime.analysisRequests.volume,
                                analysis.volume,
                                runtime.render.volume);
#endif

#ifdef CLUMP_DATA_READ
  if (services.clumpFind) {
    ExecuteClumpBatchRequest(*data.particles,
			     runtime.settings.fileNavigation,
			     runtime.snapshotLoad,
			     *services.clumpFind,
			     runtime.analysisRequests.clumpBatch,
			     runtime.analysisJobs.clumpBatch,
			     analysis.clumpBatch);
  }
#endif

#ifdef USE_CONVEX_HULL
  if (services.clumpFind && services.convexHull) {
    ExecuteConvexHullRequests(*data.particles,
                              *services.clumpFind,
                              *services.convexHull,
                              analysis.convexHulls,
                              runtime.render.polyhedra);
  }
#endif

  if (services.projectionMap2D) {
    ProjectionFrameExecutionContext projectionCtx{
      *data.particles,
      *services.projectionMap2D,
      runtime.quantity.units,
      runtime.settings.normalization.toPhysicalScale()
    };

    ProjectionMapExecutionContext projectionMapCtx{
      projectionCtx,
      camera,
      currentFileIndex,
      runtime.settings.fileNavigation.current.loadedTime,
      &runtime.analysisTools.projectionMap,
      &runtime.render.cuboidAnnotations,
      &analysis.projectionPreview
    };
    ExecuteProjectionMapRequests(runtime.analysisRequests.projectionMapRequest,
                                 projectionMapCtx);

    ProjectionMovieExecutionContext movieCtx{
      projectionCtx,
      runtime.settings.tracking,
      runtime.settings.fileNavigation,
      runtime.analysisTools.projectionMap.params,
      camera,
      runtime.analysisRequests.projectionMovie,
      runtime.analysisJobs.projectionMovie,
      runtime.snapshotLoad,
      runtime.snapshotPostprocess,
      analysis.projectionMovie
    };
    ExecuteProjectionMovieRequest(movieCtx);
  }
}
