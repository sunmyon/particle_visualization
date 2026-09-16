#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>

struct RemoteFrameTrigger {
  std::uint64_t sequence = 0;
  std::uint64_t ticket = 0;
  std::chrono::steady_clock::time_point receivedAt{};
};

// Coordinates the input receiver and frame presenter without coupling either
// side to ZeroMQ. Multiple changes collapse into one pending frame.
class RemoteFrameFlowControl {
public:
  void setBoundedTransport(bool enabled)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    boundedTransport_ = enabled;
  }

  bool boundedTransport() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return boundedTransport_;
  }

  void acknowledge(std::uint64_t frameId)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto received = outstanding_.find(frameId);
    if (received == outstanding_.end()) return;
    const bool idle = received->second;
    for (auto it = outstanding_.begin(); it != outstanding_.end();) {
      if (it->first <= frameId && it->second == idle)
        it = outstanding_.erase(it);
      else
        ++it;
    }
  }

  void release(std::uint64_t ticket)
  {
    if (!ticket) return;
    std::lock_guard<std::mutex> lock(mutex_);
    reservations_.erase(ticket);
  }

  void requeue(const RemoteFrameTrigger& trigger)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    reservations_.erase(trigger.ticket);
    dirty_ = true;
    viewerReady_ = true;
    if (trigger.sequence >= trigger_.sequence) {
      trigger_.sequence = trigger.sequence;
      trigger_.receivedAt = trigger.receivedAt;
    }
  }

  void committed(std::uint64_t ticket, std::uint64_t frameId)
  {
    if (!ticket) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = reservations_.find(ticket);
    if (it == reservations_.end()) return;
    outstanding_[frameId] = it->second;
    reservations_.erase(it);
  }

  std::size_t outstandingCount() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return reservations_.size() + outstanding_.size();
  }

  bool hasCapacity(bool idlePresentation) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!boundedTransport_) return true;
    std::size_t count = 0;
    for (const auto& entry : reservations_) count += entry.second == idlePresentation;
    for (const auto& entry : outstanding_) count += entry.second == idlePresentation;
    return count < (idlePresentation ? 1u : 2u);
  }

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

  bool tryBeginFrame(RemoteFrameTrigger* trigger = nullptr,
                     bool idlePresentation = false)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!dirty_ || !viewerReady_) return false;
    if (boundedTransport_) {
      std::size_t count = 0;
      for (const auto& entry : reservations_) count += entry.second == idlePresentation;
      for (const auto& entry : outstanding_) count += entry.second == idlePresentation;
      if (count >= (idlePresentation ? 1u : 2u)) return false;
    }
    dirty_ = false;
    viewerReady_ = false;
    if (trigger) {
      *trigger = trigger_;
      if (boundedTransport_) {
        trigger->ticket = ++nextTicket_;
        reservations_[trigger->ticket] = idlePresentation;
      }
    }
    trigger_ = {};
    return true;
  }

private:
  mutable std::mutex mutex_;
  std::uint64_t latestInputSequence_ = 0;
  std::uint64_t nextTicket_ = 0;
  bool boundedTransport_ = false;
  std::map<std::uint64_t, bool> reservations_;
  std::map<std::uint64_t, bool> outstanding_;
  bool dirty_ = true;
  bool viewerReady_ = false;
  RemoteFrameTrigger trigger_;
};
