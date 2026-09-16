#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

struct RemoteFrameTrigger {
  std::uint64_t sequence = 0;
  std::chrono::steady_clock::time_point receivedAt{};
};

// Coordinates the input receiver and frame presenter without coupling either
// side to ZeroMQ. Multiple changes collapse into one pending frame.
class RemoteFrameFlowControl {
public:
  void markDirty()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    dirty_ = true;
  }

  void markViewerReady(
    std::uint64_t sequence = 0,
    std::chrono::steady_clock::time_point receivedAt =
      std::chrono::steady_clock::now())
  {
    std::lock_guard<std::mutex> lock(mutex_);
    viewerReady_ = true;
    if (sequence >= trigger_.sequence) {
      trigger_.sequence = sequence;
      trigger_.receivedAt = receivedAt;
    }
    if (sequence > latestInputSequence_) latestInputSequence_ = sequence;
  }

  std::uint64_t latestInputSequence() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return latestInputSequence_;
  }

  bool tryBeginFrame(RemoteFrameTrigger* trigger = nullptr)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!dirty_ || !viewerReady_) return false;
    dirty_ = false;
    viewerReady_ = false;
    if (trigger) {
      *trigger = trigger_;
    }
    trigger_ = {};
    return true;
  }

private:
  mutable std::mutex mutex_;
  std::uint64_t latestInputSequence_ = 0;
  bool dirty_ = true;
  bool viewerReady_ = false;
  RemoteFrameTrigger trigger_;
};
