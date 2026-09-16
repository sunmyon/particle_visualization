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

bool BoundedRemoteTransport()
{
  const char* mode = std::getenv("PARTICLE_VIS_REMOTE_TRANSPORT");
  return mode && std::string(mode) == "bounded";
}

std::string RemoteStillEndpoint()
{
  const char* value = std::getenv("PARTICLE_VIS_REMOTE_STILL_ENDPOINT");
  return value && value[0] ? value : "tcp://127.0.0.1:5562";
}

std::int64_t MonotonicNanoseconds(std::chrono::steady_clock::time_point value)
{
  if (value.time_since_epoch().count() == 0) return 0;
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    value.time_since_epoch()).count();
}

} // namespace

#ifdef PYTHON_BRIDGE
namespace {

struct FrameToEncode {
  RenderedFrame frame;
  uint64_t triggerSequence = 0;
  uint64_t cameraGeneration = 0;
  uint64_t ticket = 0;
  int displayWidth = 0;
  int displayHeight = 0;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;
  bool idlePresentation = false;
  double readbackMs = 0.0;
  double readbackLatencyMs = 0.0;
  double triggerToReadbackMs = 0.0;
  double inputToFrameStartMs = 0.0;
  double frameToRenderMs = 0.0;
  double renderMs = 0.0;
  std::chrono::steady_clock::time_point inputReceivedAt{};
  std::chrono::steady_clock::time_point cameraUpdatedAt{};
  std::chrono::steady_clock::time_point renderStartedAt{};
  std::chrono::steady_clock::time_point renderFinishedAt{};
  std::chrono::steady_clock::time_point queuedAt;
};

struct ReadbackMetadata {
  uint64_t frameId = 0;
  uint64_t triggerSequence = 0;
  uint64_t cameraGeneration = 0;
  uint64_t ticket = 0;
  int displayWidth = 0;
  int displayHeight = 0;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;
  bool idlePresentation = false;
  double triggerToReadbackMs = 0.0;
  double inputToFrameStartMs = 0.0;
  double frameToRenderMs = 0.0;
  double renderMs = 0.0;
  std::chrono::steady_clock::time_point inputReceivedAt{};
  std::chrono::steady_clock::time_point cameraUpdatedAt{};
  std::chrono::steady_clock::time_point renderStartedAt{};
  std::chrono::steady_clock::time_point renderFinishedAt{};
  std::chrono::steady_clock::time_point submittedAt;
};

struct EncodedRemoteFrame {
  uint64_t frameId = 0;
  uint64_t triggerSequence = 0;
  uint64_t cameraGeneration = 0;
  uint64_t ticket = 0;
  int width = 0;
  int height = 0;
  int displayWidth = 0;
  int displayHeight = 0;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;
  std::string type = "rgba_frame";
  std::string format = "RGBA8";
  bool keyFrame = true;
  bool idlePresentation = false;
  std::size_t rawBytes = 0;
  std::vector<unsigned char> payload;
  double readbackMs = 0.0;
  double readbackLatencyMs = 0.0;
  double triggerToReadbackMs = 0.0;
  double inputToFrameStartMs = 0.0;
  double frameToRenderMs = 0.0;
  double renderMs = 0.0;
  double encoderQueueMs = 0.0;
  double encodeMs = 0.0;
  std::chrono::steady_clock::time_point inputReceivedAt{};
  std::chrono::steady_clock::time_point cameraUpdatedAt{};
  std::chrono::steady_clock::time_point renderStartedAt{};
  std::chrono::steady_clock::time_point renderFinishedAt{};
  std::chrono::steady_clock::time_point encodeStartedAt{};
  std::chrono::steady_clock::time_point encodedAt{};
};

} // namespace

struct RemoteFramePresenter::Impl {
  zmq::context_t context{1};
  zmq::socket_t socket;
  zmq::socket_t stillSocket;
  bool boundedTransport;

  explicit Impl(bool bounded)
    : socket(context, bounded ? zmq::socket_type::push : zmq::socket_type::pub)
    , stillSocket(context, zmq::socket_type::push)
    , boundedTransport(bounded) {}

  std::mutex encoderMutex;
  std::condition_variable interactiveEncoderWake;
  std::condition_variable idleEncoderWake;
  std::optional<FrameToEncode> pendingInteractiveFrame;
  std::optional<FrameToEncode> pendingIdleFrame;
  std::optional<EncodedRemoteFrame> completedInteractiveFrame;
  std::optional<EncodedRemoteFrame> completedIdleFrame;
  std::thread interactiveEncoderThread;
  std::thread idleEncoderThread;
  bool stopEncoder = false;
  bool sendingInteractive = false;
  int jpegQuality = 80;
  bool preferVideo = false;
  int videoBitrate = 5'000'000;
  float videoFramesPerSecond = 10.0f;
  RemoteVideoEncoder videoEncoder;
  RemoteFrameFlowControl* flowControl = nullptr;
  uint64_t latestEnqueuedFrameId = 0;
  double previousSendMs = 0.0;
  std::int64_t previousSendAcceptedAtNs = 0;
  std::uint64_t sendBackpressureCount = 0;
  std::deque<ReadbackMetadata> readbacks;

  ~Impl()
  {
    {
      std::lock_guard<std::mutex> lock(encoderMutex);
      stopEncoder = true;
    }
    interactiveEncoderWake.notify_one();
    idleEncoderWake.notify_one();
    if (interactiveEncoderThread.joinable()) {
      interactiveEncoderThread.join();
    }
    if (idleEncoderThread.joinable()) {
      idleEncoderThread.join();
    }
  }

  std::optional<EncodedRemoteFrame> encode(FrameToEncode input,
                                           bool allowVideo)
  {
    EncodedRemoteFrame output;
    output.frameId = input.frame.frameId;
    output.triggerSequence = input.triggerSequence;
    output.cameraGeneration = input.cameraGeneration;
    output.ticket = input.ticket;
    output.width = input.frame.width;
    output.height = input.frame.height;
    output.displayWidth = input.displayWidth;
    output.displayHeight = input.displayHeight;
    output.framebufferScaleX = input.framebufferScaleX;
    output.framebufferScaleY = input.framebufferScaleY;
    output.idlePresentation = input.idlePresentation;
    output.rawBytes = input.frame.pixels.size();
    output.readbackMs = input.readbackMs;
    output.readbackLatencyMs = input.readbackLatencyMs;
    output.triggerToReadbackMs = input.triggerToReadbackMs;
    output.inputToFrameStartMs = input.inputToFrameStartMs;
    output.frameToRenderMs = input.frameToRenderMs;
    output.renderMs = input.renderMs;
    output.inputReceivedAt = input.inputReceivedAt;
    output.cameraUpdatedAt = input.cameraUpdatedAt;
    output.renderStartedAt = input.renderStartedAt;
    output.renderFinishedAt = input.renderFinishedAt;
    output.encoderQueueMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - input.queuedAt).count();

    const auto encodeStart = std::chrono::steady_clock::now();
    output.encodeStartedAt = encodeStart;
    RemoteVideoPacket videoPacket;
    const RemoteVideoEncodeResult videoResult = allowVideo && preferVideo
      ? videoEncoder.encodeRgba(input.frame.width,
                                input.frame.height,
                                input.frame.pixels,
                                videoBitrate,
                                videoFramesPerSecond,
                                videoPacket)
      : RemoteVideoEncodeResult::Failed;
    if (videoResult == RemoteVideoEncodeResult::Encoded) {
      output.type = "h264_frame";
      output.format = "H264_ANNEX_B";
      output.keyFrame = videoPacket.keyFrame;
      output.payload = std::move(videoPacket.bytes);
    } else if (videoResult == RemoteVideoEncodeResult::Skipped) {
      // OpenH264 deliberately skips frames to honor its bitrate target. Sending
      // a JPEG here would defeat that control and build a network backlog.
      return std::nullopt;
    } else if (jpegQuality > 0 &&
               EncodeJpegRgba(input.frame.width,
                              input.frame.height,
                              input.frame.pixels,
                              jpegQuality,
                              output.payload)) {
      output.type = "jpeg_frame";
      output.format = "JPEG";
      output.keyFrame = true;
      if (allowVideo && preferVideo) {
        videoEncoder.requestKeyFrame();
      }
    } else {
      output.payload = std::move(input.frame.pixels);
    }
    output.encodeMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - encodeStart).count();
    output.encodedAt = std::chrono::steady_clock::now();
    return output;
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
    interactiveEncoderThread = std::thread([this]() {
      for (;;) {
        FrameToEncode input;
        {
          std::unique_lock<std::mutex> lock(encoderMutex);
          interactiveEncoderWake.wait(lock, [this]() {
            return stopEncoder ||
              (pendingInteractiveFrame.has_value() &&
               (!boundedTransport ||
                (!completedInteractiveFrame.has_value() && !sendingInteractive)));
          });
          if (stopEncoder) {
            return;
          }
          input = std::move(*pendingInteractiveFrame);
          pendingInteractiveFrame.reset();
        }

        if (flowControl &&
            flowControl->latestInputSequence() > input.cameraGeneration) {
          flowControl->release(input.ticket);
          continue;
        }
        const uint64_t ticket = input.ticket;
        const uint64_t triggerSequence = input.triggerSequence;
        auto output = encode(std::move(input), true);
        if (!output) {
          if (flowControl) flowControl->release(ticket);
          // A skipped frame produces no viewer acknowledgement. Re-arm the
          // latest-frame handshake here so the video stream cannot stall.
          if (flowControl) {
            flowControl->markDirty();
            flowControl->markViewerReady(triggerSequence);
          }
          continue;
        }
        {
          std::lock_guard<std::mutex> lock(encoderMutex);
          if (completedInteractiveFrame && flowControl)
            flowControl->release(completedInteractiveFrame->ticket);
          completedInteractiveFrame = std::move(*output);
        }
      }
    });
    idleEncoderThread = std::thread([this]() {
      for (;;) {
        FrameToEncode input;
        {
          std::unique_lock<std::mutex> lock(encoderMutex);
          idleEncoderWake.wait(lock, [this]() {
            return stopEncoder || pendingIdleFrame.has_value();
          });
          if (stopEncoder) {
            return;
          }
          input = std::move(*pendingIdleFrame);
          pendingIdleFrame.reset();
        }

        if (flowControl &&
            flowControl->latestInputSequence() > input.cameraGeneration) {
          flowControl->release(input.ticket);
          continue;
        }
        const uint64_t ticket = input.ticket;
        auto output = encode(std::move(input), false);
        if (!output) {
          if (flowControl) flowControl->release(ticket);
          continue;
        }
        {
          std::lock_guard<std::mutex> lock(encoderMutex);
          if (output->frameId >= latestEnqueuedFrameId) {
            if (completedIdleFrame && flowControl)
              flowControl->release(completedIdleFrame->ticket);
            completedIdleFrame = std::move(*output);
          } else if (flowControl) {
            flowControl->release(ticket);
          }
        }
      }
    });
  }

  void enqueue(FrameToEncode frame)
  {
    const bool idlePresentation = frame.idlePresentation;
    {
      std::lock_guard<std::mutex> lock(encoderMutex);
      latestEnqueuedFrameId =
        std::max(latestEnqueuedFrameId, frame.frame.frameId);
      if (frame.idlePresentation) {
        if (pendingIdleFrame && flowControl)
          flowControl->release(pendingIdleFrame->ticket);
        pendingIdleFrame = std::move(frame);
      } else {
        if (pendingIdleFrame && flowControl)
          flowControl->release(pendingIdleFrame->ticket);
        pendingIdleFrame.reset();
        if (pendingInteractiveFrame && flowControl)
          flowControl->release(pendingInteractiveFrame->ticket);
        pendingInteractiveFrame = std::move(frame);
      }
    }
    if (idlePresentation) {
      idleEncoderWake.notify_one();
    } else {
      interactiveEncoderWake.notify_one();
    }
  }

  void noteLatestFrame(uint64_t frameId, bool idlePresentation)
  {
    std::lock_guard<std::mutex> lock(encoderMutex);
    latestEnqueuedFrameId = std::max(latestEnqueuedFrameId, frameId);
    if (!idlePresentation) {
      if (pendingIdleFrame && flowControl)
        flowControl->release(pendingIdleFrame->ticket);
      pendingIdleFrame.reset();
    }
  }

  std::optional<EncodedRemoteFrame> takeCompleted()
  {
    std::lock_guard<std::mutex> lock(encoderMutex);
    if (completedInteractiveFrame) {
      auto result = std::move(completedInteractiveFrame);
      completedInteractiveFrame.reset();
      if (boundedTransport) sendingInteractive = true;
      else interactiveEncoderWake.notify_one();
      return result;
    }
    if (!completedIdleFrame) {
      return std::nullopt;
    }
    if (completedIdleFrame->frameId < latestEnqueuedFrameId) {
      if (flowControl) flowControl->release(completedIdleFrame->ticket);
      completedIdleFrame.reset();
      return std::nullopt;
    }
    auto result = std::move(completedIdleFrame);
    completedIdleFrame.reset();
    return result;
  }

  std::size_t applicationQueueDepth()
  {
    std::lock_guard<std::mutex> lock(encoderMutex);
    return static_cast<std::size_t>(pendingInteractiveFrame.has_value()) +
      static_cast<std::size_t>(pendingIdleFrame.has_value()) +
      static_cast<std::size_t>(completedInteractiveFrame.has_value()) +
      static_cast<std::size_t>(completedIdleFrame.has_value());
  }

  void publishCompleted()
  {
    auto encoded = takeCompleted();
    if (!encoded) {
      return;
    }
    if (encoded->idlePresentation && flowControl &&
        flowControl->latestInputSequence() > encoded->cameraGeneration) {
      flowControl->release(encoded->ticket);
      return;
    }
    const double encodeToSendMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - encoded->encodedAt).count();
    const auto sendAttemptAt = std::chrono::steady_clock::now();
    nlohmann::json header{
      {"type", encoded->type},
      {"frameId", encoded->frameId},
      {"triggerSequence", encoded->triggerSequence},
      {"cameraGeneration", encoded->cameraGeneration},
      {"latestCameraGeneration", flowControl ? flowControl->latestInputSequence() : 0},
      {"outstandingFrames", flowControl ? flowControl->outstandingCount() : 0},
      {"applicationQueueDepth", applicationQueueDepth()},
      {"sendBackpressureCount", sendBackpressureCount},
      {"serverInputReceivedAtNs", MonotonicNanoseconds(encoded->inputReceivedAt)},
      {"serverCameraUpdatedAtNs", MonotonicNanoseconds(encoded->cameraUpdatedAt)},
      {"serverRenderStartedAtNs", MonotonicNanoseconds(encoded->renderStartedAt)},
      {"serverRenderFinishedAtNs", MonotonicNanoseconds(encoded->renderFinishedAt)},
      {"serverEncodeStartedAtNs", MonotonicNanoseconds(encoded->encodeStartedAt)},
      {"serverEncodeFinishedAtNs", MonotonicNanoseconds(encoded->encodedAt)},
      {"serverSendAttemptAtNs", MonotonicNanoseconds(sendAttemptAt)},
      {"serverPreviousSendAcceptedAtNs", previousSendAcceptedAtNs},
      {"width", encoded->width},
      {"height", encoded->height},
      {"displayWidth", encoded->displayWidth},
      {"displayHeight", encoded->displayHeight},
      {"framebufferScaleX", encoded->framebufferScaleX},
      {"framebufferScaleY", encoded->framebufferScaleY},
      {"format", encoded->format},
      {"keyFrame", encoded->keyFrame},
      {"presentationMode", encoded->idlePresentation ? "idle" : "interactive"},
      {"bytes", encoded->payload.size()},
      {"rawBytes", encoded->rawBytes},
      {"serverReadbackMs", encoded->readbackMs},
      {"serverReadbackLatencyMs", encoded->readbackLatencyMs},
      {"serverTriggerToReadbackMs", encoded->triggerToReadbackMs},
      {"serverInputToFrameStartMs", encoded->inputToFrameStartMs},
      {"serverFrameToRenderMs", encoded->frameToRenderMs},
      {"serverRenderMs", encoded->renderMs},
      {"serverEncoderQueueMs", encoded->encoderQueueMs},
      {"serverEncodeMs", encoded->encodeMs},
      {"serverEncodeToSendMs", encodeToSendMs},
      {"serverPreviousSendMs", previousSendMs}
    };
    const std::string headerText = header.dump();
    const auto sendStart = std::chrono::steady_clock::now();
    zmq::socket_t& destination = boundedTransport && encoded->idlePresentation
      ? stillSocket : socket;
    bool accepted = false;
    try {
      const auto headerOk = destination.send(
        zmq::buffer(headerText),
        zmq::send_flags::sndmore | zmq::send_flags::dontwait);
      if (headerOk) {
        accepted = destination.send(zmq::buffer(encoded->payload),
                                    zmq::send_flags::dontwait).has_value();
      }
    } catch (const zmq::error_t&) {
      // The viewer requests another frame after every successful receive.
    }
    if (flowControl) {
      if (accepted) flowControl->committed(encoded->ticket, encoded->frameId);
      else {
        ++sendBackpressureCount;
        flowControl->release(encoded->ticket);
        flowControl->markDirty();
        flowControl->markViewerReady(encoded->triggerSequence);
        if (encoded->type == "h264_frame") videoEncoder.requestKeyFrame();
      }
    }
    if (boundedTransport && !encoded->idlePresentation) {
      {
        std::lock_guard<std::mutex> lock(encoderMutex);
        sendingInteractive = false;
      }
      interactiveEncoderWake.notify_one();
    }
    if (accepted) previousSendAcceptedAtNs =
      MonotonicNanoseconds(std::chrono::steady_clock::now());
    previousSendMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - sendStart).count();
  }
};
#else
struct RemoteFramePresenter::Impl { explicit Impl(bool) {} };
#endif

RemoteFramePresenter::RemoteFramePresenter(WindowContext& window,
                                           GraphicsContext& graphics,
                                           const std::string& endpoint,
                                           RemoteFrameFlowControl* flowControl)
  : window_(&window)
  , graphics_(&graphics)
  , flowControl_(flowControl)
  , endpoint_(endpoint)
  , impl_(std::make_unique<Impl>(BoundedRemoteTransport()))
{
  maxFramesPerSecond_ = RemoteMaxFramesPerSecond();
  jpegQuality_ = RemoteJpegQuality();
  preferVideo_ = PreferRemoteVideo();
  videoBitrate_ = RemoteVideoBitrate();
#ifdef PYTHON_BRIDGE
  try {
    impl_->flowControl = flowControl_;
    if (flowControl_) flowControl_->setBoundedTransport(impl_->boundedTransport);
    impl_->socket.set(zmq::sockopt::sndhwm, impl_->boundedTransport ? 2 : 1);
    impl_->socket.set(zmq::sockopt::sndtimeo, impl_->boundedTransport ? 0 : 100);
    impl_->socket.set(zmq::sockopt::linger, 0);
    if (impl_->boundedTransport) {
      impl_->socket.set(zmq::sockopt::immediate, 1);
      impl_->socket.set(zmq::sockopt::sndbuf, 65536);
      impl_->stillSocket.set(zmq::sockopt::sndhwm, 1);
      impl_->stillSocket.set(zmq::sockopt::sndtimeo, 0);
      impl_->stillSocket.set(zmq::sockopt::linger, 0);
      impl_->stillSocket.set(zmq::sockopt::immediate, 1);
      impl_->stillSocket.set(zmq::sockopt::sndbuf, 65536);
    }
    impl_->socket.bind(endpoint_);
    if (impl_->boundedTransport) {
      const std::string stillEndpoint = RemoteStillEndpoint();
      impl_->stillSocket.bind(stillEndpoint);
      std::cerr << "Remote still frames: " << stillEndpoint << '\n';
    }
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

bool RemoteFramePresenter::shouldRender(std::uint64_t appliedGeneration) const
{
  return !flowControl_ || !flowControl_->boundedTransport() ||
    (flowControl_->latestInputSequence() <=
       std::max(appliedGeneration_, appliedGeneration) &&
     flowControl_->hasCapacity(idlePresentation_));
}

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
  idlePresentation_ = size.idlePresentation;
  nextFrameTime_ = {};
  return true;
}

PresentResult RemoteFramePresenter::present(const PresentOptions& options)
{
  appliedGeneration_ = std::max(appliedGeneration_, options.appliedGeneration);
  if (options.cameraUpdatedAt.time_since_epoch().count() != 0)
    cameraUpdatedAt_ = options.cameraUpdatedAt;
  if (flowControl_ && options.contentChanged) {
    flowControl_->markDirty();
  }
  const auto now = std::chrono::steady_clock::now();
  const bool pacingAllowsFrame =
    maxFramesPerSecond_ == 0.0 || nextFrameTime_.time_since_epoch().count() == 0 ||
    now >= nextFrameTime_;
  RemoteFrameTrigger trigger;
  const bool frameIsCurrent = !flowControl_ || !flowControl_->boundedTransport() ||
    flowControl_->latestInputSequence() <= appliedGeneration_;
  const bool publishDue = active_ && pacingAllowsFrame &&
    frameIsCurrent &&
    (!flowControl_ || flowControl_->tryBeginFrame(&trigger, idlePresentation_));
  const bool supersededDuringFrame = publishDue && flowControl_ &&
    flowControl_->boundedTransport() &&
    trigger.sequence > appliedGeneration_;
  if (supersededDuringFrame) flowControl_->requeue(trigger);
  PresentOptions localOptions = options;
  localOptions.readbackFrame = options.readbackFrame ||
    (publishDue && !supersededDuringFrame);
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
      frame.triggerSequence = metadata.triggerSequence;
      frame.cameraGeneration = metadata.cameraGeneration;
      frame.ticket = metadata.ticket;
      frame.displayWidth = metadata.displayWidth;
      frame.displayHeight = metadata.displayHeight;
      frame.framebufferScaleX = metadata.framebufferScaleX;
      frame.framebufferScaleY = metadata.framebufferScaleY;
      frame.idlePresentation = metadata.idlePresentation;
      frame.readbackMs = result.readbackMs;
      frame.readbackLatencyMs =
        std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - metadata.submittedAt).count();
      frame.triggerToReadbackMs = metadata.triggerToReadbackMs;
      frame.inputToFrameStartMs = metadata.inputToFrameStartMs;
      frame.frameToRenderMs = metadata.frameToRenderMs;
      frame.renderMs = metadata.renderMs;
      frame.inputReceivedAt = metadata.inputReceivedAt;
      frame.cameraUpdatedAt = metadata.cameraUpdatedAt;
      frame.renderStartedAt = metadata.renderStartedAt;
      frame.renderFinishedAt = metadata.renderFinishedAt;
      frame.queuedAt = std::chrono::steady_clock::now();
      impl_->enqueue(std::move(frame));
    }

    if (publishDue && !supersededDuringFrame) {
      if (result.readbackSubmitted) {
        ReadbackMetadata metadata;
        metadata.frameId = ++frameId_;
        metadata.triggerSequence = trigger.sequence;
        metadata.cameraGeneration = appliedGeneration_;
        metadata.ticket = trigger.ticket;
        metadata.displayWidth = window_->displayWidth();
        metadata.displayHeight = window_->displayHeight();
        metadata.framebufferScaleX = window_->framebufferScaleX();
        metadata.framebufferScaleY = window_->framebufferScaleY();
        metadata.idlePresentation = idlePresentation_;
        metadata.frameToRenderMs = std::chrono::duration<double, std::milli>(
          options.renderStartedAt - options.frameStartedAt).count();
        metadata.renderMs = std::chrono::duration<double, std::milli>(
          options.renderFinishedAt - options.renderStartedAt).count();
        metadata.inputReceivedAt = trigger.receivedAt;
        metadata.cameraUpdatedAt = cameraUpdatedAt_;
        metadata.renderStartedAt = options.renderStartedAt;
        metadata.renderFinishedAt = options.renderFinishedAt;
        if (trigger.receivedAt.time_since_epoch().count() != 0 &&
            options.frameStartedAt >= trigger.receivedAt) {
          metadata.inputToFrameStartMs =
            std::chrono::duration<double, std::milli>(
              options.frameStartedAt - trigger.receivedAt).count();
        }
        if (trigger.receivedAt.time_since_epoch().count() != 0) {
          metadata.triggerToReadbackMs =
            std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - trigger.receivedAt).count();
        }
        metadata.submittedAt = std::chrono::steady_clock::now();
        impl_->noteLatestFrame(metadata.frameId, metadata.idlePresentation);
        impl_->readbacks.push_back(std::move(metadata));
        if (maxFramesPerSecond_ > 0.0) {
          nextFrameTime_ = now +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<double>(1.0 / maxFramesPerSecond_));
        }
      } else if (flowControl_) {
        flowControl_->release(trigger.ticket);
        flowControl_->markDirty();
        flowControl_->markViewerReady();
      }
    }

    impl_->publishCompleted();
    return result;
  }
#endif

  if (!publishDue || supersededDuringFrame || !result.frame.valid()) {
    if (publishDue && !supersededDuringFrame && flowControl_)
      flowControl_->release(trigger.ticket);
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
  frame.triggerSequence = trigger.sequence;
  frame.cameraGeneration = appliedGeneration_;
  frame.ticket = trigger.ticket;
  frame.displayHeight = window_->displayHeight();
  frame.framebufferScaleX = window_->framebufferScaleX();
  frame.framebufferScaleY = window_->framebufferScaleY();
  frame.idlePresentation = idlePresentation_;
  frame.readbackMs = result.readbackMs;
  frame.readbackLatencyMs = result.readbackMs;
  frame.frameToRenderMs = std::chrono::duration<double, std::milli>(
    options.renderStartedAt - options.frameStartedAt).count();
  frame.renderMs = std::chrono::duration<double, std::milli>(
    options.renderFinishedAt - options.renderStartedAt).count();
  frame.inputReceivedAt = trigger.receivedAt;
  frame.cameraUpdatedAt = cameraUpdatedAt_;
  frame.renderStartedAt = options.renderStartedAt;
  frame.renderFinishedAt = options.renderFinishedAt;
  if (trigger.receivedAt.time_since_epoch().count() != 0 &&
      options.frameStartedAt >= trigger.receivedAt) {
    frame.inputToFrameStartMs = std::chrono::duration<double, std::milli>(
      options.frameStartedAt - trigger.receivedAt).count();
  }
  if (trigger.receivedAt.time_since_epoch().count() != 0) {
    frame.triggerToReadbackMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - trigger.receivedAt).count();
  }
  frame.queuedAt = std::chrono::steady_clock::now();
  impl_->enqueue(std::move(frame));
  impl_->publishCompleted();
#endif

  return result;
}
