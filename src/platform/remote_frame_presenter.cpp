#include "platform/remote_frame_presenter.h"

#include <iostream>

#include <nlohmann/json.hpp>
#include <zmq.hpp>

struct RemoteFramePresenter::Impl {
  zmq::context_t context{1};
  zmq::socket_t socket{context, zmq::socket_type::pub};
};

RemoteFramePresenter::RemoteFramePresenter(WindowContext& window,
                                           const std::string& endpoint)
  : window_(&window)
  , endpoint_(endpoint)
  , impl_(std::make_unique<Impl>())
{
  try {
    impl_->socket.set(zmq::sockopt::sndhwm, 2);
    impl_->socket.bind(endpoint_);
    active_ = true;
  } catch (const zmq::error_t& e) {
    active_ = false;
    std::cerr << "RemoteFramePresenter failed to bind " << endpoint_
              << ": " << e.what() << '\n';
  }
}

RemoteFramePresenter::~RemoteFramePresenter() = default;

PresentResult RemoteFramePresenter::present(const PresentOptions& options)
{
  PresentOptions localOptions = options;
  localOptions.readbackFrame = true;

  PresentResult result =
    window_ ? PresentLocalFrame(*window_, localOptions) : PresentResult{};

  if (!active_ || !result.frame.valid()) {
    return result;
  }

  result.frame.frameId = ++frameId_;

  nlohmann::json header{
    {"type", "rgba_frame"},
    {"frameId", result.frame.frameId},
    {"width", result.frame.width},
    {"height", result.frame.height},
    {"format", "RGBA8"},
    {"bytes", result.frame.pixels.size()}
  };

  const std::string headerText = header.dump();

  try {
    const auto headerOk =
      impl_->socket.send(zmq::buffer(headerText),
                         zmq::send_flags::sndmore | zmq::send_flags::dontwait);
    if (!headerOk) {
      return result;
    }

    impl_->socket.send(zmq::buffer(result.frame.pixels),
                       zmq::send_flags::dontwait);
  } catch (const zmq::error_t&) {
    // Dropping frames is acceptable for the prototype path.
  }

  return result;
}
