#include "app/app_tool_window_dispatch.h"

#include "app/analysis_state.h"
#include "app/app_tool_window_execution.h"
#include "app/tool_window_state.h"
#include "compute_2D_histogram.h"
#include "data/particle_array.h"
#include "interaction/camera.h"

void ExecuteAnalysisToolRequests(ToolWindowUIState& tools,
                                 AnalysisToolExecutionInput& input)
{
  Histogram2DContext histCtx;
  histCtx.cameraCenter = &input.camera.cameraTarget;
  auto visibleHulls = input.analysis.convexHulls.visibleHulls();
  histCtx.convexHulls = &visibleHulls;

  ExecuteRadialProfileWindowRequests(tools.radialProfile,
                                     tools.radialProfileRequest,
                                     input.analysis.radial,
                                     input.particles.particleBlock,
                                     input.camera.cameraTarget,
                                     input.normalization,
                                     input.quantity);

  ExecuteHistogram2DWindowRequests(tools.histogram2D,
                                   tools.histogram2DRequest,
                                   input.analysis.hist2D,
                                   input.particles.particleBlock,
                                   histCtx);
}
