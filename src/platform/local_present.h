#pragma once

class WindowContext;
class OpenGLContext;

#include "render/rendered_frame.h"

struct PresentOptions {
  bool readbackFrame = false;
};

struct PresentResult {
  bool presented = false;
  RenderedFrame frame;
};

class IFramePresenter {
public:
  virtual ~IFramePresenter() = default;
  virtual PresentResult present(const PresentOptions& options = {}) = 0;
};

class LocalFramePresenter final : public IFramePresenter {
public:
  LocalFramePresenter(WindowContext& window, OpenGLContext& graphics);

  PresentResult present(const PresentOptions& options = {}) override;

private:
  WindowContext* window_ = nullptr;
  OpenGLContext* graphics_ = nullptr;
};

PresentResult PresentLocalFrame(WindowContext& window,
                                OpenGLContext& graphics,
                                const PresentOptions& options = {});
