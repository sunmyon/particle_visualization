#pragma once

#include <memory>
#include <string>

#include "platform/local_present.h"

class RemoteFramePresenter final : public IFramePresenter {
public:
  RemoteFramePresenter(WindowContext& window,
                       OpenGLContext& graphics,
                       const std::string& endpoint);
  ~RemoteFramePresenter() override;

  PresentResult present(const PresentOptions& options = {}) override;

  const std::string& endpoint() const { return endpoint_; }
  bool active() const { return active_; }

private:
  struct Impl;

  WindowContext* window_ = nullptr;
  OpenGLContext* graphics_ = nullptr;
  std::string endpoint_;
  std::unique_ptr<Impl> impl_;
  bool active_ = false;
  uint64_t frameId_ = 0;
};
