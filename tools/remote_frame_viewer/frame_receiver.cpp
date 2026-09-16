#include "frame_receiver.h"
#include "platform/remote_video_codec.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <utility>
#include <nlohmann/json.hpp>
#include <zmq.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {
bool ReceiveFrame(zmq::socket_t& sub,
                  RemoteFrame& out,
                  RemoteVideoDecoder& videoDecoder)
{
  zmq::message_t headerMsg;
  auto headerResult = sub.recv(headerMsg, zmq::recv_flags::dontwait);
  if (!headerResult) {
    return false;
  }
  if (!sub.get(zmq::sockopt::rcvmore)) return false;
  const auto headerReceivedAt = std::chrono::steady_clock::now();

  zmq::message_t payloadMsg;
  auto payloadResult = sub.recv(payloadMsg, zmq::recv_flags::none);
  if (!payloadResult) {
    return false;
  }
  const auto payloadReceivedAt = std::chrono::steady_clock::now();

  nlohmann::json header =
    nlohmann::json::parse(headerMsg.to_string(), nullptr, false);
  if (header.is_discarded()) {
    return false;
  }

  const std::string type = header.value("type", "");
  if (type != "rgba_frame" && type != "jpeg_frame" && type != "h264_frame") {
    return false;
  }

  const int width = header.value("width", 0);
  const int height = header.value("height", 0);
  const size_t expected =
    static_cast<size_t>(std::max(width, 0)) *
    static_cast<size_t>(std::max(height, 0)) * 4;
  if (width <= 0 || height <= 0) {
    return false;
  }

  if (type == "rgba_frame" && payloadMsg.size() != expected) return false;

  out.frameId = header.value("frameId", uint64_t{0});
  out.triggerSequence = header.value("triggerSequence", uint64_t{0});
  out.cameraGeneration = header.value("cameraGeneration", out.triggerSequence);
  out.latestCameraGeneration =
    header.value("latestCameraGeneration", out.cameraGeneration);
  out.outstandingFrames = header.value("outstandingFrames", std::size_t{0});
  out.applicationQueueDepth =
    header.value("applicationQueueDepth", std::size_t{0});
  out.sendBackpressureCount =
    header.value("sendBackpressureCount", uint64_t{0});
  out.serverInputReceivedAtNs =
    header.value("serverInputReceivedAtNs", int64_t{0});
  out.serverCameraUpdatedAtNs =
    header.value("serverCameraUpdatedAtNs", int64_t{0});
  out.serverRenderStartedAtNs =
    header.value("serverRenderStartedAtNs", int64_t{0});
  out.serverRenderFinishedAtNs =
    header.value("serverRenderFinishedAtNs", int64_t{0});
  out.serverEncodeStartedAtNs =
    header.value("serverEncodeStartedAtNs", int64_t{0});
  out.serverEncodeFinishedAtNs =
    header.value("serverEncodeFinishedAtNs", int64_t{0});
  out.serverSendAttemptAtNs =
    header.value("serverSendAttemptAtNs", int64_t{0});
  out.width = width;
  out.height = height;
  out.displayWidth = header.value("displayWidth", width);
  out.displayHeight = header.value("displayHeight", height);
  out.framebufferScaleX = header.value("framebufferScaleX", 1.0f);
  out.framebufferScaleY = header.value("framebufferScaleY", 1.0f);
  out.encoding = header.value("format", std::string("RGBA8"));
  out.idlePresentation = header.value("presentationMode", std::string()) == "idle";
  out.payloadBytes = payloadMsg.size();
  out.serverReadbackMs = header.value("serverReadbackMs", 0.0);
  out.serverReadbackLatencyMs =
    header.value("serverReadbackLatencyMs", out.serverReadbackMs);
  out.serverTriggerToReadbackMs =
    header.value("serverTriggerToReadbackMs", 0.0);
  out.serverInputToFrameStartMs =
    header.value("serverInputToFrameStartMs", 0.0);
  out.serverFrameToRenderMs = header.value("serverFrameToRenderMs", 0.0);
  out.serverRenderMs = header.value("serverRenderMs", 0.0);
  out.serverEncoderQueueMs = header.value("serverEncoderQueueMs", 0.0);
  out.serverEncodeMs = header.value("serverEncodeMs", 0.0);
  out.serverEncodeToSendMs = header.value("serverEncodeToSendMs", 0.0);
  out.serverPreviousSendMs = header.value("serverPreviousSendMs", 0.0);
  out.clientPayloadReceiveMs = std::chrono::duration<double, std::milli>(
    payloadReceivedAt - headerReceivedAt).count();
  out.receivedAt = payloadReceivedAt;
  const auto decodeStart = std::chrono::steady_clock::now();
  if (type == "h264_frame") {
    if (!videoDecoder.decode(
          static_cast<const unsigned char*>(payloadMsg.data()),
          payloadMsg.size(), width, height, out.rgba)) {
      return false;
    }
  } else if (type == "jpeg_frame") {
    int decodedWidth = 0;
    int decodedHeight = 0;
    int channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(
      static_cast<const stbi_uc*>(payloadMsg.data()),
      static_cast<int>(payloadMsg.size()),
      &decodedWidth,
      &decodedHeight,
      &channels,
      4);
    if (!decoded || decodedWidth != width || decodedHeight != height) {
      stbi_image_free(decoded);
      return false;
    }
    out.rgba.assign(decoded, decoded + expected);
    stbi_image_free(decoded);
  } else {
    out.rgba.resize(payloadMsg.size());
    std::copy(static_cast<const uint8_t*>(payloadMsg.data()),
              static_cast<const uint8_t*>(payloadMsg.data()) + payloadMsg.size(),
              out.rgba.begin());
  }
  out.decodedAt = std::chrono::steady_clock::now();
  out.clientDecodeMs = std::chrono::duration<double, std::milli>(
    out.decodedAt - decodeStart).count();
  return true;
}

} // namespace

struct RemoteFrameReceiver::Impl {
  std::atomic<bool> stop{false};
  std::mutex mutex;
  RemoteFrameBatch pending;
  std::thread worker;

  Impl(zmq::context_t& context, std::string endpoint,
       std::string stillEndpoint, bool bounded)
    : worker([this, &context, endpoint = std::move(endpoint),
              stillEndpoint = std::move(stillEndpoint), bounded] {
      try {
        zmq::socket_t video(context, bounded ? zmq::socket_type::pull
                                            : zmq::socket_type::sub);
        video.set(zmq::sockopt::linger, 0);
        video.set(zmq::sockopt::rcvhwm, 2);
        video.set(zmq::sockopt::rcvtimeo, 100);
        if (!bounded) video.set(zmq::sockopt::subscribe, "");
        video.connect(endpoint);
        std::unique_ptr<zmq::socket_t> still;
        if (bounded) {
          still = std::make_unique<zmq::socket_t>(context, zmq::socket_type::pull);
          still->set(zmq::sockopt::linger, 0);
          still->set(zmq::sockopt::rcvhwm, 1);
          still->set(zmq::sockopt::rcvtimeo, 100);
          still->connect(stillEndpoint);
        }
        RemoteVideoDecoder videoDecoder, stillDecoder;
        while (!stop.load()) {
          zmq::pollitem_t items[] = {
            {static_cast<void*>(video), 0, ZMQ_POLLIN, 0},
            {still ? static_cast<void*>(*still) : nullptr, 0, ZMQ_POLLIN, 0}
          };
          zmq::poll(items, still ? 2 : 1, std::chrono::milliseconds(10));
          // Service both channels once per iteration to avoid starving stills.
          for (int i = 0; i < (still ? 2 : 1) && !stop.load(); ++i) {
            if (!(items[i].revents & ZMQ_POLLIN)) continue;
            RemoteFrame frame;
            if (!ReceiveFrame(i == 0 ? video : *still, frame,
                              i == 0 ? videoDecoder : stillDecoder)) continue;
            // Pixel decoding and allocation happen outside the UI mutex.
            std::lock_guard<std::mutex> lock(mutex);
            auto& slot = frame.idlePresentation ? pending.still : pending.video;
            slot = std::move(frame);
          }
        }
      } catch (const std::exception& error) {
        std::lock_guard<std::mutex> lock(mutex);
        pending.error = error.what();
      }
    }) {}

  ~Impl() {
    stop.store(true);
    if (worker.joinable()) worker.join();
  }
};

RemoteFrameReceiver::RemoteFrameReceiver(zmq::context_t& context,
    std::string endpoint, std::string stillEndpoint, bool bounded)
  : impl_(std::make_unique<Impl>(context, std::move(endpoint),
                                std::move(stillEndpoint), bounded)) {}
RemoteFrameReceiver::~RemoteFrameReceiver() = default;

RemoteFrameBatch RemoteFrameReceiver::take()
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  RemoteFrameBatch result = std::move(impl_->pending);
  impl_->pending = {};
  return result;
}
