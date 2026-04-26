#pragma once

#include <memory>
#include <string>

#include "interaction/input_event.h"

class RemoteInputReceiver {
public:
  RemoteInputReceiver();
  ~RemoteInputReceiver();

  RemoteInputReceiver(const RemoteInputReceiver&) = delete;
  RemoteInputReceiver& operator=(const RemoteInputReceiver&) = delete;

  bool start(const std::string& endpoint, InputEventQueue& queue);
  void stop();

  bool active() const { return active_; }
  const std::string& endpoint() const { return endpoint_; }

private:
  struct Impl;

  std::unique_ptr<Impl> impl_;
  std::string endpoint_;
  bool active_ = false;
};
