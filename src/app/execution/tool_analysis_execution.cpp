#include "app/execution/tool_window_dispatch.h"

#include "app/state/analysis_state.h"
#include "app/execution/tool_window_execution.h"
#include "app/state/tool_window_state.h"
#include "analysis/histogram2d.h"
#include "data/simulation_dataset.h"
#include "data/simulation_block.h"
#include "interaction/camera.h"

void ExecuteAnalysisToolRequests(ToolWindowUIState& tools,
                                 AnalysisToolExecutionInput& input)
{
  QuantityStateScope quantityScope(input.quantity);

  const float worldToRenderScale =
    input.particles.simulationBlock.worldToRenderScale;
  const glm::vec3 analysisCenter =
    input.camera.cameraTarget / worldToRenderScale;

  Histogram2DContext histCtx;
  histCtx.dataCenter = &analysisCenter;
  histCtx.dataRadius =
    tools.histogram2DRequest.params.cameraRadius / worldToRenderScale;
#ifdef USE_CONVEX_HULL
  auto visibleHulls = input.analysis.convexHulls.visibleHulls();
  histCtx.convexHulls = &visibleHulls;
#endif

  ExecuteRadialProfileWindowRequests(tools.radialProfile,
                                     tools.radialProfileRequest,
                                     input.analysis.radial,
                                     input.particles.simulationBlock,
                                     analysisCenter,
                                     input.quantity);

  ExecuteHistogram2DWindowRequests(tools.histogram2D,
                                   tools.histogram2DRequest,
                                   input.analysis.hist2D,
                                   input.particles.simulationBlock,
                                   histCtx);
}
