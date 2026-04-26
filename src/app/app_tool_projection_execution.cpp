#include "app/app_tool_window_dispatch.h"

#include "app/app_clump_window_execution.h"
#include "app/app_tool_window_execution.h"
#include "app/tool_window_state.h"
#include "projection/projection_map_tool_state.h"

void ExecuteProjectionToolRequests(ToolWindowUIState& tools,
                                   ProjectionToolExecutionInput& input)
{
  if (input.projectionMap2D) {
    ExecuteProjectionFontSelectionRequests(tools.projectionMap,
                                           tools.projectionFontSelectionRequest,
                                           *input.projectionMap2D);
  }

  if (input.projectionMap2D && input.clumpChain) {
    ExecuteClumpChainWindowRequests(tools.clumpChain,
                                    *input.clumpChain,
                                    input.particles,
                                    input.units,
                                    *input.projectionMap2D,
                                    input.projectionMap.params,
                                    input.snapshotCurrent,
                                    input.snapshotLoad,
                                    input.camera,
                                    input.normalization);
  }
}
