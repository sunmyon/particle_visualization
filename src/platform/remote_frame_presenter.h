#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "platform/local_present.h"

class RemoteFrameFlowControl;

class RemoteFramePresenter final : public IFramePresenter {
public:
  RemoteFramePresenter(WindowContext& window,
                       GraphicsContext& graphics,
                       const std::string& endpoint,
                       RemoteFrameFlowControl* flowControl = nullptr);
  ~RemoteFramePresenter() override;

  bool shouldRender(std::uint64_t appliedGeneration) const override;
  PresentResult present(const PresentOptions& options = {}) override;
  bool resize(const PresentationSize& size) override;

  const std::string& endpoint() const { return endpoint_; }
  bool active() const { return active_; }

private:
  struct Impl;

  WindowContext* window_ = nullptr;
  GraphicsContext* graphics_ = nullptr;
  RemoteFrameFlowControl* flowControl_ = nullptr;
  std::string endpoint_;
  std::unique_ptr<Impl> impl_;
  bool active_ = false;
  uint64_t frameId_ = 0;
  uint64_t appliedGeneration_ = 0;
  std::chrono::steady_clock::time_point cameraUpdatedAt_{};
  double maxFramesPerSecond_ = 10.0;
  int jpegQuality_ = 80;
  int videoBitrate_ = 5'000'000;
  bool preferVideo_ = false;
  bool idlePresentation_ = false;
  std::chrono::steady_clock::time_point nextFrameTime_{};
};
