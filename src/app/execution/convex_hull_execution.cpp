#include <utility>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <cmath>
#include <cstdlib>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include "app/execution/analysis_execution.h"
#include "app/state/app_state.h"
#include "app/state/analysis_state.h"
#include "app/state/runtime_state.h"
#include "app/state/normalization_config.h"
#include "app/state/tracking_view_state.h"
#include "app/state/snapshot_state_sync.h"
#include "app/execution/snapshot_sequence_job.h"
#include "app/app_visibility_actions.h"
#include "app/app_data_actions.h"
#include "data/simulation_dataset.h"
#include "data/particle_selection.h"
#include "data/clump_loader.h"
#include "data/clump_store.h"
#include "data/halo_store.h"
#include "render/scene_objects.h"
#ifdef USE_CONVEX_HULL
#include "analysis/clump/find_clumps.h"
#include "analysis/convex_hull/convex_hull_generator.h"
#include "app/state/convex_hull_state.h"
#endif
#ifdef USE_CONVEX_HULL
void ExecuteConvexHullRequests(SimulationDataset& particles,
                               FindClump& clumpFind,
                               ConvexHullGenerator& convexHull,
                               ConvexHullRuntimeState& convexState,
                               RenderLayerState& polyhedraState)
{
  if (!clumpFind.checkClumpComputation()) {
    return;
  }
  if (!clumpFind.isDirty()) {
    return;
  }

  convexState.resetGroup("convex_hull");

  const int nclumps = clumpFind.get_nclumps();
  for (int i = 0; i < nclumps; ++i) {
    if (!clumpFind.flagShowHull(i)) {
      continue;
    }

    std::vector<SimulationElement> pts =
      clumpFind.get_particle_indices(i, particles.simulationBlock.particles);

    std::vector<glm::vec3> points;
    points.reserve(pts.size());
    for (const auto& p : pts) {
      points.emplace_back(p.position[0], p.position[1], p.position[2]);
    }

    ConvexHullEntry entry;
    entry.hull = convexHull.buildHull(points);
    entry.tag = "convex_hull";
    entry.sourceId = i;
    entry.visible = static_cast<bool>(entry.hull);
    entry.lineVertices = convexHull.buildLineVertices(points);

    convexState.entries.push_back(std::move(entry));
  }

  clumpFind.clearDirtyFlag();
  polyhedraState.show = !convexState.entries.empty();
  polyhedraState.cpuUpdated = true;
}
#endif
