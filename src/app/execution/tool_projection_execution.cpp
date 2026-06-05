#include "app/execution/tool_window_dispatch.h"

#include "app/execution/clump_window_execution.h"
#include "app/execution/tool_window_execution.h"
#include "app/state/tool_window_state.h"
#include "data/simulation_block.h"
#include "projection/projection_map_tool_state.h"

void ExecuteProjectionToolRequests(ToolWindowUIState& tools,
                                   ProjectionToolExecutionInput& input)
{
  QuantityStateScope quantityScope(input.quantity);

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
