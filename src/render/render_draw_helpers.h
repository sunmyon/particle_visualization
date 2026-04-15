#pragma once

#include "render/object_renderer.h"
#include "render/render_resources.h"
#include "app/runtime_state.h"

template<typename BuildFunc, typename ManagerT, typename RenderDataT>
inline void RebuildIfNeeded(bool& cpuUpdated,
                            bool& gpuDirty,
                            BuildFunc buildFunc,
                            const ManagerT& manager,
                            const RenderLayerState& runtime,
                            RenderDataT& out)
{
  if (!cpuUpdated) return;
  buildFunc(manager, runtime, out);
  cpuUpdated = false;
  gpuDirty = true;
}

template<typename RendererT, typename RenderDataT>
inline void SyncIfNeeded(RendererT& renderer,
                         RenderDataT& data,
                         bool& gpuDirty)
{
  if (!gpuDirty) return;
  renderer.sync(data);
  gpuDirty = false;
}

template<typename RendererT, typename RenderDataT>
inline void SyncAndDraw(RendererT& renderer,
                        RenderDataT& data,
                        bool& gpuDirty,
                        const RenderDrawContext& ctx,
                        const RenderLayerState& runtime)
{
  if (gpuDirty) {
    renderer.sync(data);
    gpuDirty = false;
  }
  renderer.draw(ctx, runtime);
}
