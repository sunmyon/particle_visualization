# Deferred Work

This document records work that should not be forgotten, but is intentionally
paused because the current implementation needs a larger design pass or a
better test environment.

## Isocontour Spatial Tree Balancing

Status: deferred.

The isocontour path still needs a robust and fast 2:1 spatial-tree balance
implementation. The current balance step is functionally important, but it can
be extremely slow for highly refined trees because neighbor checks repeatedly
walk leaf neighborhoods after refinement.

What remains:

- Keep 2:1 balance mandatory for isocontour mesh generation.
- Replace the current repeated leaf-neighbor scan with a more scalable local
  propagation algorithm, likely using linear octree keys or a persistent
  neighbor/cache structure.
- Add timing counters for tree build, balance, field sampling, and mesh
  extraction so regressions are visible.
- Re-test both VTK and marching-cubes paths after the balance rewrite.

