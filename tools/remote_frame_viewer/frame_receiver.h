#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zmq { class context_t; }

struct RemoteFrame {
  uint64_t frameId = 0;
  uint64_t triggerSequence = 0;
  uint64_t cameraGeneration = 0;
  uint64_t latestCameraGeneration = 0;
  std::size_t outstandingFrames = 0;
  std::size_t applicationQueueDepth = 0;
  uint64_t sendBackpressureCount = 0;
  int64_t serverInputReceivedAtNs = 0;
  int64_t serverCameraUpdatedAtNs = 0;
  int64_t serverRenderStartedAtNs = 0;
  int64_t serverRenderFinishedAtNs = 0;
  int64_t serverEncodeStartedAtNs = 0;
  int64_t serverEncodeFinishedAtNs = 0;
  int64_t serverSendAttemptAtNs = 0;
  int width = 0;
  int height = 0;
  int displayWidth = 0;
  int displayHeight = 0;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;
  std::string encoding = "RGBA8";
  bool idlePresentation = false;
  std::size_t payloadBytes = 0;
  double serverReadbackMs = 0.0;
  double serverReadbackLatencyMs = 0.0;
  double serverTriggerToReadbackMs = 0.0;
  double serverInputToFrameStartMs = 0.0;
  double serverFrameToRenderMs = 0.0;
  double serverRenderMs = 0.0;
  double serverEncoderQueueMs = 0.0;
  double serverEncodeMs = 0.0;
  double serverEncodeToSendMs = 0.0;
  double serverPreviousSendMs = 0.0;
  double clientPayloadReceiveMs = 0.0;
  double clientDecodeMs = 0.0;
  double clientInputToReceiveMs = -1.0;
  std::chrono::steady_clock::time_point receivedAt{};
  std::chrono::steady_clock::time_point decodedAt{};
  std::chrono::steady_clock::time_point inputSentAt{};
  std::vector<uint8_t> rgba;
};

struct RemoteFrameBatch {
  std::optional<RemoteFrame> video;
  std::optional<RemoteFrame> still;
  std::string error;
};

// Owns all receive sockets and decoder state on a worker thread. The UI takes
// at most one completed frame per channel; encoded H.264 frames are never skipped.
class RemoteFrameReceiver {
public:
  RemoteFrameReceiver(zmq::context_t& context, std::string endpoint,
                      std::string stillEndpoint, bool bounded);
  ~RemoteFrameReceiver();
  RemoteFrameReceiver(const RemoteFrameReceiver&) = delete;
  RemoteFrameReceiver& operator=(const RemoteFrameReceiver&) = delete;
  RemoteFrameBatch take();
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
