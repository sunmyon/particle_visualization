#include "platform/remote_frame_presenter.h"

#include "platform/graphics_context.h"
#include "platform/remote_frame_flow_control.h"
#include "platform/window_context.h"
#include "image/image_io.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>

#ifdef PYTHON_BRIDGE
#include <nlohmann/json.hpp>
#include <zmq.hpp>
#endif

namespace {

double RemoteMaxFramesPerSecond()
{
  const char* value = std::getenv("PARTICLE_VIS_REMOTE_MAX_FPS");
  if (!value || value[0] == '\0') {
    return 10.0;
  }
  try {
    const double parsed = std::stod(value);
    return parsed >= 0.0 ? parsed : 10.0;
  } catch (...) {
    return 10.0;
  }
}

int RemoteJpegQuality()
{
  const char* value = std::getenv("PARTICLE_VIS_REMOTE_JPEG_QUALITY");
  if (!value || value[0] == '\0') return 80;
  try {
    return std::clamp(std::stoi(value), 0, 100);
  } catch (...) {
    return 80;
  }
}

} // namespace

#ifdef PYTHON_BRIDGE
struct RemoteFramePresenter::Impl {
  zmq::context_t context{1};
  zmq::socket_t socket{context, zmq::socket_type::pub};
};
#else
struct RemoteFramePresenter::Impl {};
#endif

RemoteFramePresenter::RemoteFramePresenter(WindowContext& window,
                                           GraphicsContext& graphics,
                                           const std::string& endpoint,
                                           RemoteFrameFlowControl* flowControl)
  : window_(&window)
  , graphics_(&graphics)
  , flowControl_(flowControl)
  , endpoint_(endpoint)
  , impl_(std::make_unique<Impl>())
{
  maxFramesPerSecond_ = RemoteMaxFramesPerSecond();
  jpegQuality_ = RemoteJpegQuality();
#ifdef PYTHON_BRIDGE
  try {
    impl_->socket.set(zmq::sockopt::sndhwm, 1);
    impl_->socket.set(zmq::sockopt::sndtimeo, 100);
    impl_->socket.set(zmq::sockopt::linger, 0);
    impl_->socket.bind(endpoint_);
    active_ = true;
    std::cerr << "Remote frame limit: " << maxFramesPerSecond_
              << " FPS (0 disables pacing)\n";
    std::cerr << "Remote frame encoding: "
              << (jpegQuality_ > 0
                    ? "JPEG quality " + std::to_string(jpegQuality_)
                    : "raw RGBA")
              << '\n';
  } catch (const zmq::error_t& e) {
    active_ = false;
    std::cerr << "RemoteFramePresenter failed to bind " << endpoint_
              << ": " << e.what() << '\n';
  }
#else
  std::cerr << "RemoteFramePresenter disabled: build without PYTHON_BRIDGE/ZMQ. "
            << "Endpoint ignored: " << endpoint_ << '\n';
  active_ = false;
#endif
}

RemoteFramePresenter::~RemoteFramePresenter() = default;

bool RemoteFramePresenter::resize(const PresentationSize& size)
{
  if (!window_ || !graphics_ || !window_->isHeadless() ||
      !graphics_->resizeHeadless(size.framebufferWidth,
                                 size.framebufferHeight)) {
    return false;
  }
  window_->updateRemoteFramebufferSize(size.framebufferWidth,
                                       size.framebufferHeight,
                                       size.displayWidth,
                                       size.displayHeight,
                                       size.framebufferScaleX,
                                       size.framebufferScaleY);
  nextFrameTime_ = {};
  return true;
}

PresentResult RemoteFramePresenter::present(const PresentOptions& options)
{
  if (flowControl_ && options.contentChanged) {
    flowControl_->markDirty();
  }
  const auto now = std::chrono::steady_clock::now();
  const bool pacingAllowsFrame =
    maxFramesPerSecond_ == 0.0 || nextFrameTime_.time_since_epoch().count() == 0 ||
    now >= nextFrameTime_;
  const bool publishDue = active_ && pacingAllowsFrame &&
    (!flowControl_ || flowControl_->tryBeginFrame());
  PresentOptions localOptions = options;
  localOptions.readbackFrame = options.readbackFrame || publishDue;

  PresentResult result =
    (window_ && graphics_)
      ? PresentLocalFrame(*window_, *graphics_, localOptions)
      : PresentResult{};

  if (!publishDue || !result.frame.valid()) {
    return result;
  }

  if (maxFramesPerSecond_ > 0.0) {
    nextFrameTime_ = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(1.0 / maxFramesPerSecond_));
  }

#ifdef PYTHON_BRIDGE
  result.frame.frameId = ++frameId_;

  std::vector<unsigned char> encoded;
  const bool useJpeg =
    jpegQuality_ > 0 &&
    EncodeJpegRgba(result.frame.width,
                   result.frame.height,
                   result.frame.pixels,
                   jpegQuality_,
                   encoded);
  const auto& payload = useJpeg ? encoded : result.frame.pixels;

  nlohmann::json header{
    {"type", useJpeg ? "jpeg_frame" : "rgba_frame"},
    {"frameId", result.frame.frameId},
    {"width", result.frame.width},
    {"height", result.frame.height},
    {"displayWidth", window_->displayWidth()},
    {"displayHeight", window_->displayHeight()},
    {"framebufferScaleX", window_->framebufferScaleX()},
    {"framebufferScaleY", window_->framebufferScaleY()},
    {"format", useJpeg ? "JPEG" : "RGBA8"},
    {"bytes", payload.size()},
    {"rawBytes", result.frame.pixels.size()}
  };

  const std::string headerText = header.dump();

  try {
    const auto headerOk =
      impl_->socket.send(zmq::buffer(headerText),
                         zmq::send_flags::sndmore | zmq::send_flags::dontwait);
    if (!headerOk) {
      return result;
    }

    impl_->socket.send(zmq::buffer(payload), zmq::send_flags::none);
  } catch (const zmq::error_t&) {
    // Dropping frames is acceptable for the prototype path.
  }
#endif

  return result;
}
