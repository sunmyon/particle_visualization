#include "platform/remote_frame_presenter.h"

#include "platform/graphics_context.h"
#include "platform/remote_frame_flow_control.h"
#include "platform/remote_video_codec.h"
#include "platform/window_context.h"
#include "image/image_io.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

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

bool PreferRemoteVideo()
{
  const char* value = std::getenv("PARTICLE_VIS_REMOTE_CODEC");
  if (!value || value[0] == '\0') return true;
  return std::string(value) == "h264" || std::string(value) == "H264";
}

int RemoteVideoBitrate()
{
  const char* value = std::getenv("PARTICLE_VIS_REMOTE_VIDEO_BITRATE");
  if (!value || value[0] == '\0') return 5'000'000;
  try {
    return std::clamp(std::stoi(value), 250'000, 100'000'000);
  } catch (...) {
    return 5'000'000;
  }
}

} // namespace

#ifdef PYTHON_BRIDGE
namespace {

struct FrameToEncode {
  RenderedFrame frame;
  int displayWidth = 0;
  int displayHeight = 0;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;
  double readbackMs = 0.0;
  double readbackLatencyMs = 0.0;
  std::chrono::steady_clock::time_point queuedAt;
};

struct ReadbackMetadata {
  uint64_t frameId = 0;
  int displayWidth = 0;
  int displayHeight = 0;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;
  std::chrono::steady_clock::time_point submittedAt;
};

struct EncodedRemoteFrame {
  uint64_t frameId = 0;
  int width = 0;
  int height = 0;
  int displayWidth = 0;
  int displayHeight = 0;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;
  std::string type = "rgba_frame";
  std::string format = "RGBA8";
  bool keyFrame = true;
  std::size_t rawBytes = 0;
  std::vector<unsigned char> payload;
  double readbackMs = 0.0;
  double readbackLatencyMs = 0.0;
  double encoderQueueMs = 0.0;
  double encodeMs = 0.0;
};

} // namespace

struct RemoteFramePresenter::Impl {
  zmq::context_t context{1};
  zmq::socket_t socket{context, zmq::socket_type::pub};

  std::mutex encoderMutex;
  std::condition_variable encoderWake;
  std::optional<FrameToEncode> pendingFrame;
  std::optional<EncodedRemoteFrame> completedFrame;
  std::thread encoderThread;
  bool stopEncoder = false;
  int jpegQuality = 80;
  bool preferVideo = false;
  int videoBitrate = 5'000'000;
  float videoFramesPerSecond = 10.0f;
  RemoteVideoEncoder videoEncoder;
  uint64_t latestEnqueuedFrameId = 0;
  std::deque<ReadbackMetadata> readbacks;

  ~Impl()
  {
    {
      std::lock_guard<std::mutex> lock(encoderMutex);
      stopEncoder = true;
    }
    encoderWake.notify_one();
    if (encoderThread.joinable()) {
      encoderThread.join();
    }
  }

  void startEncoder(int quality,
                    bool useVideo,
                    int targetBitrate,
                    float framesPerSecond)
  {
    jpegQuality = quality;
    preferVideo = useVideo && videoEncoder.available();
    videoBitrate = targetBitrate;
    videoFramesPerSecond = framesPerSecond;
    encoderThread = std::thread([this]() {
      for (;;) {
        FrameToEncode input;
        {
          std::unique_lock<std::mutex> lock(encoderMutex);
          encoderWake.wait(lock, [this]() {
            return stopEncoder || pendingFrame.has_value();
          });
          if (stopEncoder && !pendingFrame) {
            return;
          }
          input = std::move(*pendingFrame);
          pendingFrame.reset();
        }

        EncodedRemoteFrame output;
        output.frameId = input.frame.frameId;
        output.width = input.frame.width;
        output.height = input.frame.height;
        output.displayWidth = input.displayWidth;
        output.displayHeight = input.displayHeight;
        output.framebufferScaleX = input.framebufferScaleX;
        output.framebufferScaleY = input.framebufferScaleY;
        output.rawBytes = input.frame.pixels.size();
        output.readbackMs = input.readbackMs;
        output.readbackLatencyMs = input.readbackLatencyMs;
        output.encoderQueueMs = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - input.queuedAt).count();

        const auto encodeStart = std::chrono::steady_clock::now();
        RemoteVideoPacket videoPacket;
        if (preferVideo &&
            videoEncoder.encodeRgba(input.frame.width,
                                    input.frame.height,
                                    input.frame.pixels,
                                    videoBitrate,
                                    videoFramesPerSecond,
                                    videoPacket)) {
          output.type = "h264_frame";
          output.format = "H264_ANNEX_B";
          output.keyFrame = videoPacket.keyFrame;
          output.payload = std::move(videoPacket.bytes);
        } else if (jpegQuality > 0 &&
                   EncodeJpegRgba(input.frame.width,
                                  input.frame.height,
                                  input.frame.pixels,
                                  jpegQuality,
                                  output.payload)) {
          output.type = "jpeg_frame";
          output.format = "JPEG";
          output.keyFrame = true;
          if (preferVideo) videoEncoder.requestKeyFrame();
        } else {
          output.payload = std::move(input.frame.pixels);
        }
        output.encodeMs = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - encodeStart).count();

        {
          std::lock_guard<std::mutex> lock(encoderMutex);
          completedFrame = std::move(output);
        }
      }
    });
  }

  void enqueue(FrameToEncode frame)
  {
    {
      std::lock_guard<std::mutex> lock(encoderMutex);
      latestEnqueuedFrameId =
        std::max(latestEnqueuedFrameId, frame.frame.frameId);
      pendingFrame = std::move(frame);
    }
    encoderWake.notify_one();
  }

  void noteLatestFrame(uint64_t frameId)
  {
    std::lock_guard<std::mutex> lock(encoderMutex);
    latestEnqueuedFrameId = std::max(latestEnqueuedFrameId, frameId);
  }

  std::optional<EncodedRemoteFrame> takeCompleted()
  {
    std::lock_guard<std::mutex> lock(encoderMutex);
    if (!completedFrame) {
      return std::nullopt;
    }
    if (completedFrame->type != "h264_frame" &&
        completedFrame->frameId < latestEnqueuedFrameId) {
      completedFrame.reset();
      return std::nullopt;
    }
    auto result = std::move(completedFrame);
    completedFrame.reset();
    return result;
  }

  void publishCompleted()
  {
    auto encoded = takeCompleted();
    if (!encoded) {
      return;
    }
    nlohmann::json header{
      {"type", encoded->type},
      {"frameId", encoded->frameId},
      {"width", encoded->width},
      {"height", encoded->height},
      {"displayWidth", encoded->displayWidth},
      {"displayHeight", encoded->displayHeight},
      {"framebufferScaleX", encoded->framebufferScaleX},
      {"framebufferScaleY", encoded->framebufferScaleY},
      {"format", encoded->format},
      {"keyFrame", encoded->keyFrame},
      {"bytes", encoded->payload.size()},
      {"rawBytes", encoded->rawBytes},
      {"serverReadbackMs", encoded->readbackMs},
      {"serverReadbackLatencyMs", encoded->readbackLatencyMs},
      {"serverEncoderQueueMs", encoded->encoderQueueMs},
      {"serverEncodeMs", encoded->encodeMs}
    };
    const std::string headerText = header.dump();
    try {
      const auto headerOk = socket.send(
        zmq::buffer(headerText),
        zmq::send_flags::sndmore | zmq::send_flags::dontwait);
      if (headerOk) {
        socket.send(zmq::buffer(encoded->payload),
                    zmq::send_flags::none);
      }
    } catch (const zmq::error_t&) {
      // The viewer requests another frame after every successful receive.
    }
  }
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
  preferVideo_ = PreferRemoteVideo();
  videoBitrate_ = RemoteVideoBitrate();
#ifdef PYTHON_BRIDGE
  try {
    impl_->socket.set(zmq::sockopt::sndhwm, 1);
    impl_->socket.set(zmq::sockopt::sndtimeo, 100);
    impl_->socket.set(zmq::sockopt::linger, 0);
    impl_->socket.bind(endpoint_);
    impl_->startEncoder(jpegQuality_,
                        preferVideo_,
                        videoBitrate_,
                        static_cast<float>(maxFramesPerSecond_ > 0.0
                                             ? maxFramesPerSecond_
                                             : 30.0));
    active_ = true;
    std::cerr << "Remote frame limit: " << maxFramesPerSecond_
              << " FPS (0 disables pacing)\n";
    if (preferVideo_ && impl_->videoEncoder.available()) {
      std::cerr << "Remote frame encoding: H.264 at "
                << videoBitrate_ << " bit/s\n";
    } else {
      std::cerr << "Remote frame encoding: "
                << (jpegQuality_ > 0
                      ? "JPEG quality " + std::to_string(jpegQuality_)
                      : "raw RGBA") << '\n';
    }
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
#ifdef PYTHON_BRIDGE
  const bool asyncRemoteReadback =
    active_ && !options.readbackFrame && graphics_ &&
    graphics_->supportsAsyncReadback();
  localOptions.asyncReadback = asyncRemoteReadback;
#endif

  PresentResult result =
    (window_ && graphics_)
      ? PresentLocalFrame(*window_, *graphics_, localOptions)
      : PresentResult{};

#ifdef PYTHON_BRIDGE
  if (asyncRemoteReadback) {
    if (result.frame.valid() && !impl_->readbacks.empty()) {
      ReadbackMetadata metadata = std::move(impl_->readbacks.front());
      impl_->readbacks.pop_front();
      result.frame.frameId = metadata.frameId;

      FrameToEncode frame;
      frame.frame = std::move(result.frame);
      frame.displayWidth = metadata.displayWidth;
      frame.displayHeight = metadata.displayHeight;
      frame.framebufferScaleX = metadata.framebufferScaleX;
      frame.framebufferScaleY = metadata.framebufferScaleY;
      frame.readbackMs = result.readbackMs;
      frame.readbackLatencyMs =
        std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - metadata.submittedAt).count();
      frame.queuedAt = std::chrono::steady_clock::now();
      impl_->enqueue(std::move(frame));
    }

    if (publishDue) {
      if (result.readbackSubmitted) {
        ReadbackMetadata metadata;
        metadata.frameId = ++frameId_;
        metadata.displayWidth = window_->displayWidth();
        metadata.displayHeight = window_->displayHeight();
        metadata.framebufferScaleX = window_->framebufferScaleX();
        metadata.framebufferScaleY = window_->framebufferScaleY();
        metadata.submittedAt = std::chrono::steady_clock::now();
        impl_->noteLatestFrame(metadata.frameId);
        impl_->readbacks.push_back(std::move(metadata));
        if (maxFramesPerSecond_ > 0.0) {
          nextFrameTime_ = now +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<double>(1.0 / maxFramesPerSecond_));
        }
      } else if (flowControl_) {
        flowControl_->markDirty();
        flowControl_->markViewerReady();
      }
    }

    impl_->publishCompleted();
    return result;
  }
#endif

  if (!publishDue || !result.frame.valid()) {
#ifdef PYTHON_BRIDGE
    if (active_) {
      impl_->publishCompleted();
    }
#endif
    return result;
  }

  if (maxFramesPerSecond_ > 0.0) {
    nextFrameTime_ = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(1.0 / maxFramesPerSecond_));
  }

#ifdef PYTHON_BRIDGE
  result.frame.frameId = ++frameId_;
  FrameToEncode frame;
  if (options.readbackFrame) {
    frame.frame = result.frame;
  } else {
    frame.frame = std::move(result.frame);
  }
  frame.displayWidth = window_->displayWidth();
  frame.displayHeight = window_->displayHeight();
  frame.framebufferScaleX = window_->framebufferScaleX();
  frame.framebufferScaleY = window_->framebufferScaleY();
  frame.readbackMs = result.readbackMs;
  frame.readbackLatencyMs = result.readbackMs;
  frame.queuedAt = std::chrono::steady_clock::now();
  impl_->enqueue(std::move(frame));
  impl_->publishCompleted();
#endif

  return result;
}
