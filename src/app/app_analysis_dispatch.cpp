#include "app/app_analysis_dispatch.h"

#include "app/app_state.h"
#include "app/app_analysis_execution.h"
#include "app/app_projection_execution.h"
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
    ExecuteProjectionMapRequests(runtime.analysisRequests.projectionMapRequest,
                                 runtime.analysisTools.projectionMap,
                                 *services.projectionMap2D,
                                 *data.particles,
                                 runtime.quantity.units,
                                 runtime.settings.normalization,
                                 camera,
                                 runtime.render.cuboidAnnotations,
                                 currentFileIndex,
                                 analysis.projectionPreview,
                                 runtime.settings.fileNavigation.current.loadedTime);

    ExecuteProjectionMovieRequest(*data.particles,
				  runtime.quantity.units,
				  runtime.settings.normalization,
				  runtime.settings.tracking,
				  runtime.settings.fileNavigation,
				  *services.projectionMap2D,
				  runtime.analysisTools.projectionMap.params,
				  camera,
				  runtime.analysisRequests.projectionMovie,
				  runtime.analysisJobs.projectionMovie,
				  runtime.snapshotLoad,
				  runtime.snapshotPostprocess,
				  analysis.projectionMovie);
  }
}
