#include "app/app_tool_window_dispatch.h"

void ExecuteToolWindowRequests(ToolWindowUIState& tools,
                               ParticleToolExecutionInput& particleInput,
                               AnalysisToolExecutionInput& analysisInput,
                               ProjectionToolExecutionInput& projectionInput,
                               HaloToolExecutionInput& haloInput,
                               ClumpToolExecutionInput& clumpInput)
{
  ExecuteParticleToolRequests(tools, particleInput);
  ExecuteAnalysisToolRequests(tools, analysisInput);
  ExecuteProjectionToolRequests(tools, projectionInput);
  ExecuteDataFilterToolRequests(tools);
  ExecuteHaloToolRequests(tools, haloInput);
  ExecuteClumpToolRequests(tools, clumpInput);
}
