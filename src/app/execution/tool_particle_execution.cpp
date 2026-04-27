#include "app/execution/tool_window_dispatch.h"

#include "app/execution/tool_window_execution.h"
#include "app/state/tool_window_state.h"

void ExecuteParticleToolRequests(ToolWindowUIState& tools,
                                 ParticleToolExecutionInput& input)
{
  ExecuteTopParticlesWindowRequests(tools.topParticles,
                                    tools.topParticlesRequest,
                                    tools.topParticlesResult,
                                    input.particles,
                                    input.camera,
                                    input.tracking,
                                    input.snapshotPostprocess,
                                    input.quantity);
}
