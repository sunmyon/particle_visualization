#pragma once

class WindowContext;

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
  explicit LocalFramePresenter(WindowContext& window);

  PresentResult present(const PresentOptions& options = {}) override;

private:
  WindowContext* window_ = nullptr;
};

PresentResult PresentLocalFrame(WindowContext& window,
                                const PresentOptions& options = {});
