#pragma once

#include <cstdint>

#include "render/opengl/object_renderer.h"
#include "render/render_resources.h"
#include "app/state/runtime_state.h"

template<typename RendererT, typename RenderDataT>
inline void SyncIfVersionChanged(RendererT& renderer,
                                 const RenderDataT& data,
                                 std::uint64_t dataVersion,
                                 std::uint64_t& uploadedVersion)
{
  if (uploadedVersion == dataVersion) return;
  renderer.sync(data);
  uploadedVersion = dataVersion;
}

template<typename RendererT, typename RenderDataT>
inline void SyncAndDraw(RendererT& renderer,
                        const RenderDataT& data,
                        std::uint64_t dataVersion,
                        std::uint64_t& uploadedVersion,
                        const RenderDrawContext& ctx,
                        const RenderLayerState& runtime)
{
  SyncIfVersionChanged(renderer, data, dataVersion, uploadedVersion);
  renderer.draw(ctx, runtime);
}
