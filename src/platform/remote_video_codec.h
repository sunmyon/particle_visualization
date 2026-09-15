#pragma once

#include <cstdint>
#include <memory>
#include <vector>

struct RemoteVideoPacket {
  int width = 0;
  int height = 0;
  bool keyFrame = false;
  std::vector<unsigned char> bytes;
};

// Stateful H.264 codec used by the remote transport. The implementation is
// optional: available() is false when particle_vis was built without OpenH264.
class RemoteVideoEncoder {
public:
  RemoteVideoEncoder();
  ~RemoteVideoEncoder();

  RemoteVideoEncoder(const RemoteVideoEncoder&) = delete;
  RemoteVideoEncoder& operator=(const RemoteVideoEncoder&) = delete;

  bool available() const;
  bool encodeRgba(int width,
                  int height,
                  const std::vector<unsigned char>& rgba,
                  int bitrate,
                  float framesPerSecond,
                  RemoteVideoPacket& output);
  void requestKeyFrame();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class RemoteVideoDecoder {
public:
  RemoteVideoDecoder();
  ~RemoteVideoDecoder();

  RemoteVideoDecoder(const RemoteVideoDecoder&) = delete;
  RemoteVideoDecoder& operator=(const RemoteVideoDecoder&) = delete;

  bool available() const;
  bool decode(const unsigned char* data,
              std::size_t size,
              int expectedWidth,
              int expectedHeight,
              std::vector<unsigned char>& rgba);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
