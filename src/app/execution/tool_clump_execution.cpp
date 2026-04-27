#include "app/execution/tool_window_dispatch.h"

#include "app/execution/clump_window_execution.h"
#include "app/state/runtime_state.h"
#include "app/state/tool_window_state.h"
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
