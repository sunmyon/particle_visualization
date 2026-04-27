#include "app/execution/tool_window_dispatch.h"

#include "app/execution/tool_window_execution.h"
#include "app/state/tool_window_state.h"

void ExecuteHaloToolRequests(ToolWindowUIState& tools,
                             HaloToolExecutionInput& input)
{
#ifdef HAVE_HDF5
  ExecuteHaloesWindowRequests(tools.haloes,
                              tools.haloesRequest,
                              input.haloStore,
                              input.particles,
                              input.camera,
                              input.normalization);
#else
  (void)tools;
  (void)input;
#endif
}
