#include "render/render_backend.h"

#include "projection/projection_map_ui_state.h"

class NullRenderBackend final : public RenderBackend {
public:
  void init() override {}
  void destroy() override {}

  void render(const RenderFrameState&, const RenderSceneData&) override {}

  void updateProjectionPreview(const RgbImage&) override {
    preview_ = ProjectionPreviewUIState{};
  }

  ProjectionPreviewUIState makeProjectionPreviewUIState() const override {
    return preview_;
  }

  RenderBackendCapabilities capabilities() const override { return {}; }

private:
  ProjectionPreviewUIState preview_;
};

std::unique_ptr<RenderBackend> CreateNullRenderBackend()
{
  return std::make_unique<NullRenderBackend>();
}
