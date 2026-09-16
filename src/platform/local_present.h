#pragma once

#include <chrono>
#include <cstdint>

class WindowContext;
class GraphicsContext;

#include "render/rendered_frame.h"

struct PresentOptions {
  bool readbackFrame = false;
  bool asyncReadback = false;
  bool contentChanged = false;
  bool renderedScene = true;
  std::uint64_t appliedGeneration = 0;
  std::chrono::steady_clock::time_point frameStartedAt{};
  std::chrono::steady_clock::time_point cameraUpdatedAt{};
  std::chrono::steady_clock::time_point renderStartedAt{};
  std::chrono::steady_clock::time_point renderFinishedAt{};
};

struct PresentResult {
  bool presented = false;
  RenderedFrame frame;
  double readbackMs = 0.0;
  bool readbackSubmitted = false;
};

struct PresentationSize {
  int framebufferWidth = 1;
  int framebufferHeight = 1;
  int displayWidth = 1;
  int displayHeight = 1;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;
  bool idlePresentation = false;
};

class IFramePresenter {
public:
  virtual ~IFramePresenter() = default;
  virtual bool shouldRender(std::uint64_t) const { return true; }
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
