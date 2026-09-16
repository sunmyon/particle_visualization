#include "platform/remote_video_codec.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

// Deterministic moving particles plus a static UI-like panel. This is an
// encode/decode benchmark, not a network or GPU-rendering benchmark.
int Benchmark()
{
  constexpr int width = 1134, height = 712, frames = 60;
  std::vector<unsigned char> rgba(width * height * 4), decoded;
  RemoteVideoEncoder encoder;
  RemoteVideoDecoder decoder;
  std::vector<double> timings;
  std::size_t totalBytes = 0;
  double squaredError = 0.0;
  int skipped = 0;
  for (int frame = 0; frame < frames; ++frame) {
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const int i = (y * width + x) * 4;
        const unsigned char value = x < 240 ? ((y % 25 < 3) ? 150 : 40) : 8;
        rgba[i] = rgba[i + 1] = rgba[i + 2] = value;
        rgba[i + 3] = 255;
      }
    }
    unsigned int random = 12345;
    for (int point = 0; point < 20000; ++point) {
      random = random * 1664525u + 1013904223u;
      const int x = 240 + ((random >> 8) + frame * 3) % (width - 240);
      random = random * 1664525u + 1013904223u;
      const int y = ((random >> 8) + frame) % height;
      const int i = (y * width + x) * 4;
      rgba[i] = rgba[i + 1] = rgba[i + 2] = 80 + (random % 176);
    }
    RemoteVideoPacket packet;
    const auto start = std::chrono::steady_clock::now();
    const auto result = encoder.encodeRgba(width, height, rgba, 5000000, 10, packet);
    const double elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start).count();
    if (result == RemoteVideoEncodeResult::Skipped) { ++skipped; continue; }
    if (result != RemoteVideoEncodeResult::Encoded ||
        !decoder.decode(packet.bytes.data(), packet.bytes.size(), width, height, decoded)) return 1;
    timings.push_back(elapsed);
    totalBytes += packet.bytes.size();
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
      const double delta = double(rgba[i]) - decoded[i];
      squaredError += delta * delta;
    }
  }
  if (timings.empty()) return 1;
  std::sort(timings.begin(), timings.end());
  const double mse = squaredError / (timings.size() * width * height);
  std::cout << "encode median/p95 ms " << timings[timings.size()/2] << "/"
            << timings[(timings.size()-1)*95/100] << ", mean bytes "
            << totalBytes / timings.size() << ", PSNR " << 10 * std::log10(255.0*255.0/mse)
            << ", encoded/skipped " << timings.size() << "/" << skipped << '\n';
  return 0;
}

int main(int argc, char** argv)

{
  if (argc == 2 && std::string(argv[1]) == "--benchmark") return Benchmark();
  constexpr int width = 320;
  constexpr int height = 180;
  std::vector<unsigned char> rgba(width * height * 4);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
      rgba[i] = static_cast<unsigned char>(x * 255 / (width - 1));
      rgba[i + 1] = static_cast<unsigned char>(y * 255 / (height - 1));
      rgba[i + 2] = static_cast<unsigned char>((x + y) & 255);
      rgba[i + 3] = 255;
    }
  }

  RemoteVideoEncoder encoder;
  RemoteVideoDecoder decoder;
  if (!encoder.available() || !decoder.available()) return EXIT_FAILURE;

  RemoteVideoPacket packet;
  if (encoder.encodeRgba(width, height, rgba, 2'000'000, 15.0f, packet) !=
        RemoteVideoEncodeResult::Encoded ||
      !packet.keyFrame || packet.bytes.empty()) {
    std::cerr << "H.264 encoding failed\n";
    return EXIT_FAILURE;
  }

  std::vector<unsigned char> decoded;
  if (!decoder.decode(packet.bytes.data(), packet.bytes.size(), width, height, decoded) ||
      decoded.size() != rgba.size()) {
    std::cerr << "H.264 decoding failed\n";
    return EXIT_FAILURE;
  }

  double absoluteError = 0.0;
  for (std::size_t i = 0; i < rgba.size(); i += 4) {
    absoluteError += std::abs(static_cast<int>(rgba[i]) - decoded[i]);
    absoluteError += std::abs(static_cast<int>(rgba[i + 1]) - decoded[i + 1]);
    absoluteError += std::abs(static_cast<int>(rgba[i + 2]) - decoded[i + 2]);
  }
  const double meanError = absoluteError / (width * height * 3.0);
  const std::size_t keyFrameBytes = packet.bytes.size();

  // Change only a small region. This verifies that the encoder retains its
  // reference frame and that the decoder accepts the following P-frame.
  for (int y = 60; y < 100; ++y) {
    for (int x = 120; x < 180; ++x) {
      const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
      rgba[i] = 255;
      rgba[i + 1] = 40;
      rgba[i + 2] = 20;
    }
  }
  RemoteVideoPacket deltaPacket;
  std::vector<unsigned char> deltaDecoded;
  if (encoder.encodeRgba(width, height, rgba, 2'000'000, 15.0f, deltaPacket) !=
        RemoteVideoEncodeResult::Encoded ||
      deltaPacket.keyFrame ||
      !decoder.decode(deltaPacket.bytes.data(), deltaPacket.bytes.size(),
                      width, height, deltaDecoded)) {
    std::cerr << "H.264 inter-frame round trip failed\n";
    return EXIT_FAILURE;
  }

  encoder.requestKeyFrame();
  RemoteVideoPacket recovery;
  if (encoder.encodeRgba(width, height, rgba, 2'000'000, 15.0f, recovery) !=
        RemoteVideoEncodeResult::Encoded || !recovery.keyFrame ||
      !decoder.decode(recovery.bytes.data(), recovery.bytes.size(),
                      width, height, deltaDecoded)) return EXIT_FAILURE;

  // Tiny, non-macroblock-aligned resize may reduce the requested slice count.
  std::vector<unsigned char> small(34 * 18 * 4, 100);
  RemoteVideoPacket resized;
  if (encoder.encodeRgba(34, 18, small, 2'000'000, 15.0f, resized) !=
        RemoteVideoEncodeResult::Encoded || !resized.keyFrame ||
      !decoder.decode(resized.bytes.data(), resized.bytes.size(),
                      34, 18, deltaDecoded) || deltaDecoded.size() != small.size())
    return EXIT_FAILURE;

  std::cout << "H.264 round trip: " << rgba.size() << " raw bytes -> "
            << keyFrameBytes << " key-frame bytes -> "
            << deltaPacket.bytes.size() << " delta-frame bytes, mean error "
            << meanError << '\n';
  return meanError < 15.0 && deltaPacket.bytes.size() < keyFrameBytes
           ? EXIT_SUCCESS
           : EXIT_FAILURE;
}
