#include "app/execution/tool_window_dispatch.h"

#include "app/state/analysis_state.h"
#include "app/execution/tool_window_execution.h"
#include "app/state/tool_window_state.h"
#include "analysis/histogram2d.h"
#include "data/simulation_dataset.h"
#include "interaction/camera.h"

void ExecuteAnalysisToolRequests(ToolWindowUIState& tools,
                                 AnalysisToolExecutionInput& input)
{
  Histogram2DContext histCtx;
  histCtx.cameraCenter = &input.camera.cameraTarget;
  histCtx.worldToRenderScale = input.particles.simulationBlock.worldToRenderScale;
#ifdef USE_CONVEX_HULL
  auto visibleHulls = input.analysis.convexHulls.visibleHulls();
  histCtx.convexHulls = &visibleHulls;
#endif

  ExecuteRadialProfileWindowRequests(tools.radialProfile,
                                     tools.radialProfileRequest,
                                     input.analysis.radial,
                                     input.particles.simulationBlock,
                                     input.camera.cameraTarget,
                                     input.normalization,
                                     input.quantity);

  ExecuteHistogram2DWindowRequests(tools.histogram2D,
                                   tools.histogram2DRequest,
                                   input.analysis.hist2D,
                                   input.particles.simulationBlock,
                                   histCtx);
}
