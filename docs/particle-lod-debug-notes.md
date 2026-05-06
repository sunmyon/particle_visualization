# Particle LOD Debug Notes

This note records the GPU particle LOD design pitfalls discovered while making
the Metal path usable for tens of millions of particles.

## Goal

The LOD path is only useful if it behaves like a game-style stable interactive
view:

- camera motion should remain responsive;
- particles should not flicker during LOD updates;
- nearby dense clumps must stay visually coherent;
- the implementation must not build a particle-by-particle index list every
  time the camera changes.

## Failed Bottom-Up Design

The first GPU attempt dispatched one thread per tree node. Each thread walked
its parent chain toward the root to decide whether an ancestor had already
accepted a proxy.

That design was wrong for LOD selection.

Observed problems:

- LOD is a top-down decision, but the implementation was bottom-up.
- Every node was dispatched even when an accepted proxy ancestor should have
  pruned the entire subtree.
- Parent-chain walks made the work closer to `O(nodes * depth)` than
  `O(visited nodes)`.
- Many descendant threads repeatedly read the same parent chains, hurting GPU
  cache locality.
- Leaf particles were appended one by one, creating too many atomics.
- Lowering the update rate did not fix the fundamental cost of a single
  update.

Conclusion: never reintroduce all-node bottom-up LOD traversal.

## Current Direction

The intended traversal is root-to-leaf:

1. Start the frontier with the root.
2. Process only nodes in the current frontier.
3. Frustum-cull a node before expanding it.
4. Accept a node as proxy if its projected screen size is below the pixel
   threshold.
5. If a visible node is a leaf, append its contiguous particle range.
6. Only expand children when the node is visible, too large, and not a leaf.

This changes the expected work to roughly `O(visited nodes)`.

## Range-Based Drawing

The important rule is: do not generate selected particle indices.

GPU traversal output should stay compact:

- `proxyBuffer`
- `leafRangeBuffer {start, count}`
- `proxyCount`
- `leafRangeCount`
- `coveredParticleCount`

Particle drawing should use contiguous ranges. Adjacent leaf ranges should be
merged before drawing. On Metal, the practical path is to build range/indirect
draw commands from the merged ranges instead of expanding a particle index
buffer.

## Flicker Lessons

Several fixes reduced or removed flicker:

- Keep the previously valid LOD result visible until a new LOD result is fully
  ready.
- Do not clear or swap visible LOD buffers midway through an update.
- Do not fall back to normal full-particle drawing while testing LOD stability;
  that hides the actual LOD failure mode.
- Do not clamp traversal depth just to reduce work. If a near clump needs leaf
  particles, forcing a shallow proxy hides data.
- Treat projected pixel size as the main criterion. Camera-target distance and
  raw camera distance are too indirect for stable screen-space LOD.

The main remaining cost is range/command construction. In the tested Metal
path it became acceptable after switching away from particle-index generation,
but it is still slower than drawing the already-cached full particle frame.

## Debug Counters To Keep

Keep lightweight counters visible in the Performance panel:

- visited nodes
- frustum-culled nodes
- expanded nodes
- appended children
- proxy nodes
- leaf ranges
- merged leaf ranges
- covered leaf particles
- generated draw commands
- LOD traversal/update time
- range build time
- range draw encode time

These counters made it clear when LOD was not actually coarsening, when culling
was broken, and when draw command generation dominated the update.
