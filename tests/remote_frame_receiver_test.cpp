#include "frame_receiver.h"
#include "platform/remote_video_codec.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <nlohmann/json.hpp>
#include <zmq.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

using namespace std::chrono_literals;

void Check(bool condition, const char* message)
{
  if (!condition) throw std::runtime_error(message);
}

void Send(zmq::socket_t& socket, uint64_t id, bool still,
          const std::vector<unsigned char>& bytes,
          const char* type = "rgba_frame", int width = 32, int height = 32)
{
  const auto header = nlohmann::json{
    {"type", type}, {"frameId", id}, {"cameraGeneration", id},
    {"width", width}, {"height", height},
    {"presentationMode", still ? "idle" : "interactive"}
  }.dump();
  Check(socket.send(zmq::buffer(header), zmq::send_flags::sndmore).has_value(), "header send failed");
  Check(socket.send(zmq::buffer(bytes), zmq::send_flags::none).has_value(), "payload send failed");
}

RemoteFrame WaitFor(RemoteFrameReceiver& receiver, uint64_t id, bool still = false)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto batch = receiver.take();
    Check(batch.error.empty(), batch.error.c_str());
    auto& frame = still ? batch.still : batch.video;
    if (frame && frame->frameId == id) return std::move(*frame);
    std::this_thread::sleep_for(1ms);
  }
  throw std::runtime_error("frame receive timed out");
}

int main()
{
  try {
    zmq::context_t context(1);
    zmq::socket_t video(context, zmq::socket_type::push);
    zmq::socket_t still(context, zmq::socket_type::push);
    for (auto* socket : {&video, &still}) {
      socket->set(zmq::sockopt::linger, 0);
      socket->set(zmq::sockopt::sndtimeo, 2000);
    }
    video.bind("inproc://receiver-video");
    still.bind("inproc://receiver-still");
    std::vector<unsigned char> pixels(32 * 32 * 4, 120);
    {
      RemoteFrameReceiver receiver(context, "inproc://receiver-video", "inproc://receiver-still", true);
      // No frames available: UI polling must not wait for network traffic.
      const auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < 1000; ++i) Check(!receiver.take().video, "unexpected frame");
      Check(std::chrono::steady_clock::now() - start < 250ms, "UI polling blocked on network");
      Send(video, 1, false, pixels);
      Check(WaitFor(receiver, 1).rgba == pixels, "RGBA pixels changed");

      // Hold the UI while receiving independent video/still channels.
      Send(still, 2, true, pixels);
      for (uint64_t id = 3; id <= 12; ++id) Send(video, id, false, pixels);
      bool gotVideo = false, gotStill = false;
      const auto deadline = std::chrono::steady_clock::now() + 3s;
      std::this_thread::sleep_for(50ms);
      while ((!gotVideo || !gotStill) && std::chrono::steady_clock::now() < deadline) {
        auto batch = receiver.take();
        Check(batch.error.empty(), "receiver error");
        if (batch.video) gotVideo |= batch.video->frameId == 12;
        if (batch.still) gotStill |= batch.still->frameId == 2;
        std::this_thread::sleep_for(1ms);
      }
      Check(gotVideo && gotStill, "one channel displaced the other");
      Check(!receiver.take().video, "completed images accumulated in a queue");

      std::vector<unsigned char> jpeg;
      Check(stbi_write_jpg_to_func([](void* context, void* data, int size) {
        auto& bytes = *static_cast<std::vector<unsigned char>*>(context);
        const auto* begin = static_cast<unsigned char*>(data);
        bytes.insert(bytes.end(), begin, begin + size);
      }, &jpeg, 32, 32, 4, pixels.data(), 90), "JPEG encode failed");
      Send(still, 13, true, jpeg, "jpeg_frame");
      Check(WaitFor(receiver, 13, true).rgba.size() == pixels.size(), "JPEG still decode failed");

      RemoteVideoEncoder encoder;
      if (encoder.available()) {
        RemoteVideoDecoder reference;
        std::vector<unsigned char> expected;
        uint64_t last = 0;
        bool sawDelta = false;
        // Decode every compressed frame even when the UI skips presentations.
        for (uint64_t id = 14; id < 24; ++id) {
          pixels[(id * 4) % pixels.size()] = static_cast<unsigned char>(id * 7);
          RemoteVideoPacket packet;
          const auto result = encoder.encodeRgba(32, 32, pixels, 2000000, 10, packet);
          if (result == RemoteVideoEncodeResult::Skipped) continue;
          Check(result == RemoteVideoEncodeResult::Encoded, "encode failed");
          sawDelta |= !packet.keyFrame;
          Check(reference.decode(packet.bytes.data(), packet.bytes.size(), 32, 32, expected), "reference decode failed");
          Send(video, id, false, packet.bytes, "h264_frame");
          last = id;
        }
        Check(sawDelta, "test did not exercise inter-frame references");
        Check(WaitFor(receiver, last).rgba == expected, "worker lost H264 reference frames");
      }

      std::vector<unsigned char> resized(64 * 32 * 4, 230);
      Send(video, 25, false, resized, "rgba_frame", 64, 32);
      const auto frame = WaitFor(receiver, 25);
      Check(frame.width == 64 && frame.rgba == resized, "resized frame lost");
    }
    const auto start = std::chrono::steady_clock::now();
    { RemoteFrameReceiver receiver(context, "inproc://absent", "inproc://absent-still", true); }
    Check(std::chrono::steady_clock::now() - start < 1s, "shutdown waited for absent server");

    zmq::socket_t publisher(context, zmq::socket_type::pub);
    publisher.set(zmq::sockopt::linger, 0);
    publisher.bind("inproc://receiver-legacy");
    RemoteFrameReceiver legacy(context, "inproc://receiver-legacy", "", false);
    std::this_thread::sleep_for(50ms);
    Send(publisher, 30, false, pixels);
    Check(WaitFor(legacy, 30).rgba == pixels, "legacy subscription failed");
    std::cout << "Async receiver: RGBA/JPEG, H264 references, both channels, resize, legacy and shutdown passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
