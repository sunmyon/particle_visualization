# Metal Projection Debug Notes

## Render-Target Splat Experiment

Status: abandoned for projection-map replacement.

The first Metal projection attempt used render-target splatting:

- Render one circular particle footprint as a small triangle bounding box.
- Discard fragments outside the circular SPH kernel support.
- Add `sum(value * weight)` and `sum(weight)` into an offscreen color target
  with additive blending.
- Read the target back and divide the two channels on the CPU.

This is attractive because it uses the rasterizer and blend hardware, but it
proved unsuitable as the numerically stable replacement for the CPU projection
path.

Observed issues:

- `RGBA16Float` render targets saturated at `65504` for dense projections.
- `RGBA32Float` additive blending worked in tiny smoke cases, but produced an
  empty target for large overdraw on the tested Apple Metal environment.
- The output depended too strongly on render-target format behavior, which is
  not acceptable for an analysis-style projection path.

Conclusion:

- Keep render splatting for visual previews only if it is reintroduced.
- Do not use render-target blending as the canonical projection-map
  accumulation path.
- The stable GPU path should use compute kernels with explicit accumulation.

## Current Direction

The Metal projection backend now follows the CPU structure more closely:

- One compute thread handles one particle.
- The thread computes the affected pixel range.
- The kernel is evaluated at pixel centers, matching `createProjectionMap()`.
- Contributions are accumulated into float buffers with `atomic_float` adds
  when supported by the active Metal environment.

This avoids render-target float blending and keeps the implementation closer to
the CPU reference. It should still be validated against CPU output before
treating the path as production quality, because atomic accumulation order can
produce small floating-point differences.

## Voronoi Projection Prototype

The Voronoi path uses a different GPU strategy because it is a nearest-label
problem, not an additive SPH deposit:

- The initial seed label grid is currently built on the CPU by mapping each
  cell center to its nearest voxel and keeping the closest seed per voxel.
- A 3D Jump Flooding pass propagates nearest-cell labels on the GPU.
- A refinement step repeats `step=1` neighbor checks.
- A final compute pass integrates labels along the projection axis and writes
  `sum(value * weight)` and `sum(weight)` for the 2D image.

This is a prototype. It is intended to be compared against the CPU nanoflann
path for label agreement, field error, and runtime. If the approximation is
acceptable, seed-grid initialization can be moved from CPU to GPU later.

## Cross-Backend Follow-Up

The Vulkan Voronoi path now mirrors the successful Metal structure:

- Shared projection inputs live in `projection_gpu_backend.h`.
- GUI Vulkan projection reuses the active `VulkanContext`; it does not create a
  second MoltenVK instance.
- The 3D label grid is cached for repeated panels with the same geometry.
- Field changes reuse the label grid and only rerun the integration pass.

This made multi-panel Vulkan projection stable in the GUI path after repeated
JFA rebuilds had triggered `VK_ERROR_DEVICE_LOST`.

Vulkan SPH-like projection was added with the same particle-thread compute
structure as Metal. It is deliberately gated on
`shaderBufferFloat32AtomicAdd`; if that feature is missing, the code falls back
to the CPU projection instead of using render-target blending or another
approximation.
