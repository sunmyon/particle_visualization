#include "render/render_backend.h"

#include "platform/metal_context.h"
#include "projection/projection_map_ui_state.h"

#include <iostream>

class MetalRenderBackend final : public RenderBackend {
public:
  explicit MetalRenderBackend(MetalContext& context)
    : context_(&context)
  {
  }

  void init() override
  {
    std::cerr << "Metal render backend: platform initialized; scene rendering "
                 "is not implemented yet."
              << std::endl;
  }

  void destroy() override
  {
    if (context_) {
      context_->setPreImGuiDrawCallback({});
    }
  }

  void render(const RenderFrameState&, const RenderSceneData&) override {}

  void updateProjectionPreview(const RgbImage&) override
  {
    preview_ = ProjectionPreviewUIState{};
  }

  ProjectionPreviewUIState makeProjectionPreviewUIState() const override
  {
    return preview_;
  }

  RenderBackendCapabilities capabilities() const override { return {}; }

private:
  MetalContext* context_ = nullptr;
  ProjectionPreviewUIState preview_;
};

std::unique_ptr<RenderBackend> CreateMetalRenderBackend(MetalContext& context)
{
  return std::make_unique<MetalRenderBackend>(context);
}
