#include "platform/remote_video_codec.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

int main()
{
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
  if (!encoder.encodeRgba(width, height, rgba, 2'000'000, 15.0f, packet) ||
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
  if (!encoder.encodeRgba(width, height, rgba, 2'000'000, 15.0f, deltaPacket) ||
      deltaPacket.keyFrame ||
      !decoder.decode(deltaPacket.bytes.data(), deltaPacket.bytes.size(),
                      width, height, deltaDecoded)) {
    std::cerr << "H.264 inter-frame round trip failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "H.264 round trip: " << rgba.size() << " raw bytes -> "
            << keyFrameBytes << " key-frame bytes -> "
            << deltaPacket.bytes.size() << " delta-frame bytes, mean error "
            << meanError << '\n';
  return meanError < 15.0 && deltaPacket.bytes.size() < keyFrameBytes
           ? EXIT_SUCCESS
           : EXIT_FAILURE;
}
