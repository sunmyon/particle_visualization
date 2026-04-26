#include "app/app_tool_window_dispatch.h"

#include "app/app_clump_window_execution.h"
#include "app/runtime_state.h"
#include "app/tool_window_state.h"
#include "data/particle_array.h"

void ExecuteClumpToolRequests(ToolWindowUIState& tools,
                              ClumpToolExecutionInput& input)
{
  if (input.clumpFind) {
    ExecuteClumpFinderWindowRequests(tools.clumpFind,
                                     *input.clumpFind,
                                     input.particles.particleBlock.particles,
                                     input.camera,
                                     input.snapshotCurrent.loadedTime,
                                     input.snapshotInput,
                                     input.snapshotCurrent);
  }

  if (input.clumpLoad) {
    ExecuteLoadedClumpWindowRequests(tools.clumpList,
                                     *input.clumpLoad,
                                     input.clumpStore,
                                     input.tracking,
                                     input.camera,
                                     input.currentFileIndex,
                                     input.snapshotInput,
                                     input.normalization);
  }
}
