#pragma once

class WindowContext;
class GraphicsContext;

#include "render/rendered_frame.h"

struct PresentOptions {
  bool readbackFrame = false;
  bool contentChanged = false;
};

struct PresentResult {
  bool presented = false;
  RenderedFrame frame;
};

struct PresentationSize {
  int framebufferWidth = 1;
  int framebufferHeight = 1;
  int displayWidth = 1;
  int displayHeight = 1;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;
};

class IFramePresenter {
public:
  virtual ~IFramePresenter() = default;
  virtual PresentResult present(const PresentOptions& options = {}) = 0;
  virtual bool resize(const PresentationSize& size) = 0;
};

class LocalFramePresenter final : public IFramePresenter {
public:
  LocalFramePresenter(WindowContext& window, GraphicsContext& graphics);

  PresentResult present(const PresentOptions& options = {}) override;
  bool resize(const PresentationSize& size) override;

private:
  WindowContext* window_ = nullptr;
  GraphicsContext* graphics_ = nullptr;
};

PresentResult PresentLocalFrame(WindowContext& window,
                                GraphicsContext& graphics,
                                const PresentOptions& options = {});
