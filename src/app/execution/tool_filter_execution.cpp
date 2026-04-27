#include "app/execution/tool_window_dispatch.h"

#include "app/execution/tool_window_execution.h"
#include "app/state/tool_window_state.h"

void ExecuteDataFilterToolRequests(ToolWindowUIState& tools)
{
  ExecuteMaskWindowRequests(tools.mask, tools.maskRequest);
}
