#include "platform/remote_input_receiver.h"

#ifdef PYTHON_BRIDGE
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include <nlohmann/json.hpp>
#include <zmq.hpp>
#else
#include <iostream>
#endif

#ifdef PYTHON_BRIDGE
namespace {

InputEventType ParseType(const std::string& value)
{
  if (value == "pointer_scroll") return InputEventType::PointerScroll;
  if (value == "key") return InputEventType::Key;
  if (value == "framebuffer_resize") return InputEventType::FramebufferResize;
  return InputEventType::PointerMove;
}

InputKey ParseKey(const std::string& value)
{
  if (value == "Escape") return InputKey::Escape;
  return InputKey::Unknown;
}

InputAction ParseAction(const std::string& value)
{
  if (value == "Release") return InputAction::Release;
  if (value == "Repeat") return InputAction::Repeat;
  return InputAction::Press;
}

InputEvent ParseEvent(const nlohmann::json& json)
{
  InputEvent event;
  event.type = ParseType(json.value("type", "pointer_move"));
  event.x = json.value("x", 0.0f);
  event.y = json.value("y", 0.0f);
  event.wheelX = json.value("wheelX", 0.0f);
  event.wheelY = json.value("wheelY", 0.0f);
  event.width = json.value("width", 0);
  event.height = json.value("height", 0);
  event.key = ParseKey(json.value("key", ""));
  event.action = ParseAction(json.value("action", "Press"));
  event.primaryDown = json.value("primaryDown", false);
  event.capturedByUI = json.value("capturedByUI", false);

  if (json.contains("modifiers")) {
    const auto& m = json["modifiers"];
    event.modifiers.shift = m.value("shift", false);
    event.modifiers.ctrl = m.value("ctrl", false);
    event.modifiers.alt = m.value("alt", false);
    event.modifiers.super = m.value("super", false);
  }

  if (json.contains("viewport")) {
    const auto& v = json["viewport"];
    event.viewport.x = v.value("x", 0);
    event.viewport.y = v.value("y", 0);
    event.viewport.width = v.value("width", 1);
    event.viewport.height = v.value("height", 1);
    event.viewport.framebufferScaleX = v.value("framebufferScaleX", 1.0f);
    event.viewport.framebufferScaleY = v.value("framebufferScaleY", 1.0f);
  }

  return event;
}

} // namespace
#endif

#ifdef PYTHON_BRIDGE
struct RemoteInputReceiver::Impl {
  std::atomic<bool> running{false};
  std::thread thread;
  zmq::context_t context{1};
  zmq::socket_t socket{context, zmq::socket_type::pull};

  void loop(InputEventQueue* queue)
  {
    while (running.load()) {
      zmq::pollitem_t items[] = {
        {static_cast<void*>(socket), 0, ZMQ_POLLIN, 0}
      };
      zmq::poll(items, 1, std::chrono::milliseconds(10));
      if ((items[0].revents & ZMQ_POLLIN) == 0) {
        continue;
      }

      zmq::message_t msg;
      if (!socket.recv(msg, zmq::recv_flags::none)) {
        continue;
      }

      nlohmann::json json =
        nlohmann::json::parse(msg.to_string(), nullptr, false);
      if (json.is_discarded()) {
        continue;
      }

      queue->push(ParseEvent(json));
    }
  }
};
#else
struct RemoteInputReceiver::Impl {};
#endif

RemoteInputReceiver::RemoteInputReceiver() = default;

RemoteInputReceiver::~RemoteInputReceiver()
{
  stop();
}

bool RemoteInputReceiver::start(const std::string& endpoint, InputEventQueue& queue)
{
#ifndef PYTHON_BRIDGE
  (void)queue;
  endpoint_ = endpoint;
  std::cerr << "RemoteInputReceiver disabled: build without PYTHON_BRIDGE/ZMQ. "
            << "Endpoint ignored: " << endpoint_ << '\n';
  endpoint_.clear();
  active_ = false;
  return false;
#else
  if (active_) {
    return false;
  }

  endpoint_ = endpoint;
  impl_ = std::make_unique<Impl>();

  try {
    impl_->socket.set(zmq::sockopt::rcvhwm, 256);
    impl_->socket.bind(endpoint_);
  } catch (const zmq::error_t& e) {
    std::cerr << "RemoteInputReceiver failed to bind " << endpoint_
              << ": " << e.what() << '\n';
    impl_.reset();
    endpoint_.clear();
    return false;
  }

  impl_->running = true;
  impl_->thread = std::thread(&Impl::loop, impl_.get(), &queue);
  active_ = true;
  return true;
#endif
}

void RemoteInputReceiver::stop()
{
#ifndef PYTHON_BRIDGE
  active_ = false;
  endpoint_.clear();
  return;
#else
  if (!impl_) {
    active_ = false;
    return;
  }

  impl_->running = false;
  if (impl_->thread.joinable()) {
    impl_->thread.join();
  }
  impl_.reset();
  active_ = false;
  endpoint_.clear();
#endif
}
