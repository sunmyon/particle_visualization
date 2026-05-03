# Projection GPU Backends

Projection map generation is managed under `src/projection/`, not under
`src/render/`. The render backends draw already-prepared scene data; projection
backends compute image data from simulation cells.

## Shared Boundary

The shared input/output types live in:

- `src/projection/projection_gpu_backend.h`

Backend-specific implementations should use the same data contract:

- `src/projection/metal_projection_backend.mm`
- `src/projection/vulkan_projection_backend.cpp`
- `src/projection/opengl_projection_backend.cpp`

The GUI flag `ProjectionMapParams::useGpuProjection` only asks for a GPU
projection path. The concrete backend can be selected with:

```bash
PARTICLE_VIS_PROJECTION_BACKEND=auto|metal|vulkan|opengl
```

If `PARTICLE_VIS_PROJECTION_BACKEND` is unset, projection follows
`PARTICLE_VIS_RENDER_BACKEND` when that variable is set. For example,
`PARTICLE_VIS_RENDER_BACKEND=vulkan` tries Vulkan projection and then falls back
to CPU if Vulkan projection is unavailable. It does not silently switch to
Metal. If neither variable is set, `auto` tries available GPU projection
backends before CPU fallback. OpenGL is available only as an explicit diagnostic
request because the current OpenGL path cannot run the required compute
projection.

## Backend Status

- Metal: active implementation. SPH projection uses compute kernels with
  `atomic_float` accumulation. Voronoi projection uses CPU seed initialization,
  GPU 3D jump flooding, and cached label-grid integration.
- Vulkan: Voronoi JFA/integration compute kernels are wired through
  `vulkan_projection_backend.cpp`. GUI execution uses the active
  `VulkanContext` instead of creating a second Vulkan device. SPH-like
  projection now follows the Metal structure when the active device exposes
  `VK_EXT_shader_atomic_float` with `shaderBufferFloat32AtomicAdd`; otherwise
  it explicitly falls back to CPU. The standalone `vulkan_projection_smoke`
  target is useful on platforms where a second Vulkan instance can be created;
  on macOS the GUI path is the meaningful test because it reuses the window
  context.
- OpenGL: intentionally disabled for now. The window path requests OpenGL 3.3,
  while projection compute would need OpenGL 4.3 compute shaders plus reliable
  float atomic accumulation. macOS OpenGL cannot provide that path.

## Current Vulkan Notes

- Do not mix render and projection backends implicitly. If
  `PARTICLE_VIS_RENDER_BACKEND=vulkan`, projection tries Vulkan and falls back
  to CPU. It does not silently use Metal.
- The macOS GUI path must reuse the active `VulkanContext`. Creating a separate
  standalone Vulkan/MoltenVK instance can fail with
  `VK_ERROR_INCOMPATIBLE_DRIVER`.
- Multi-panel Voronoi projection needs label-grid caching. Rebuilding the 3D JFA
  label grid for every panel repeatedly allocates large buffers and can lead to
  `VK_ERROR_DEVICE_LOST` on MoltenVK. The generator now caches the Vulkan
  Voronoi label grid in the same spirit as the Metal path, so panels with the
  same geometry reuse the expensive labels and only redo field integration.
- `vulkan_projection_smoke` still exercises the standalone path. It is useful on
  systems where standalone Vulkan compute is available, but it is not the
  authoritative macOS GUI test.

## SPH-like Projection

The SPH-like projection path matches the CPU/Metal compute logic:

- One compute invocation per particle.
- Compute the affected pixel range from `hsml`.
- Evaluate the same cubic kernel at pixel centers.
- Accumulate `sum(value * weight)` and `sum(weight)`.

For Vulkan, the direct implementation requires float atomic add support in the
active device. Runtime capability detection for `VK_EXT_shader_atomic_float` /
`shaderBufferFloat32AtomicAdd` happens during `VulkanContext` device creation.
If this is not available, Vulkan keeps using CPU fallback for SPH-like
projection rather than silently switching to a different numerical method.

## Why `src/projection/`

The projection code needs to be reproducible from GUI and batch-style requests.
Keeping GPU projection implementations beside the CPU projection implementation
prevents render-backend details from leaking into analysis/execution code.
