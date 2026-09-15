#pragma once

#include <mutex>

// Coordinates the input receiver and frame presenter without coupling either
// side to ZeroMQ. Multiple changes collapse into one pending frame.
class RemoteFrameFlowControl {
public:
  void markDirty()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    dirty_ = true;
  }

  void markViewerReady()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    viewerReady_ = true;
  }

  void deferViewerFrame()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    viewerReady_ = false;
  }

  bool tryBeginFrame()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!dirty_ || !viewerReady_) return false;
    dirty_ = false;
    viewerReady_ = false;
    return true;
  }

private:
  std::mutex mutex_;
  bool dirty_ = true;
  bool viewerReady_ = false;
};
