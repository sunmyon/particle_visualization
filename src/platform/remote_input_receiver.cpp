#include "platform/remote_input_receiver.h"

#ifdef PYTHON_BRIDGE
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "platform/remote_input_protocol.h"
#include <zmq.hpp>
#else
#include <iostream>
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

      const auto event = RemoteInputProtocol::Decode(
        std::string_view(static_cast<const char*>(msg.data()), msg.size()));
      if (event) {
        queue->push(*event);
      }
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
