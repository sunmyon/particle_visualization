#include "platform/remote_video_codec.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#ifdef PARTICLE_VIS_HAVE_OPENH264
#include <wels/codec_api.h>
#endif

namespace {

#ifdef PARTICLE_VIS_HAVE_OPENH264
unsigned char ClampByte(int value)
{
  return static_cast<unsigned char>(std::clamp(value, 0, 255));
}

void RgbaToI420(int width,
                int height,
                const std::vector<unsigned char>& rgba,
                std::vector<unsigned char>& i420)
{
  const std::size_t lumaSize = static_cast<std::size_t>(width) * height;
  i420.resize(lumaSize + lumaSize / 2);
  unsigned char* yPlane = i420.data();
  unsigned char* uPlane = yPlane + lumaSize;
  unsigned char* vPlane = uPlane + lumaSize / 4;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
      const int r = rgba[index];
      const int g = rgba[index + 1];
      const int b = rgba[index + 2];
      yPlane[static_cast<std::size_t>(y) * width + x] =
        ClampByte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
    }
  }

  for (int y = 0; y < height; y += 2) {
    for (int x = 0; x < width; x += 2) {
      int sumU = 0;
      int sumV = 0;
      for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
          const std::size_t index =
            (static_cast<std::size_t>(y + dy) * width + x + dx) * 4;
          const int r = rgba[index];
          const int g = rgba[index + 1];
          const int b = rgba[index + 2];
          sumU += ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
          sumV += ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
        }
      }
      const std::size_t chromaIndex =
        static_cast<std::size_t>(y / 2) * (width / 2) + x / 2;
      uPlane[chromaIndex] = ClampByte((sumU + 2) / 4);
      vPlane[chromaIndex] = ClampByte((sumV + 2) / 4);
    }
  }
}

void I420ToRgba(const unsigned char* yPlane,
                const unsigned char* uPlane,
                const unsigned char* vPlane,
                int width,
                int height,
                int yStride,
                int uvStride,
                std::vector<unsigned char>& rgba)
{
  rgba.resize(static_cast<std::size_t>(width) * height * 4);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int yy = std::max(0, static_cast<int>(yPlane[y * yStride + x]) - 16);
      const int u = static_cast<int>(uPlane[(y / 2) * uvStride + x / 2]) - 128;
      const int v = static_cast<int>(vPlane[(y / 2) * uvStride + x / 2]) - 128;
      const int c = 298 * yy;
      const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
      rgba[index] = ClampByte((c + 409 * v + 128) >> 8);
      rgba[index + 1] = ClampByte((c - 100 * u - 208 * v + 128) >> 8);
      rgba[index + 2] = ClampByte((c + 516 * u + 128) >> 8);
      rgba[index + 3] = 255;
    }
  }
}
#endif

} // namespace

struct RemoteVideoEncoder::Impl {
#ifdef PARTICLE_VIS_HAVE_OPENH264
  ISVCEncoder* encoder = nullptr;
  int width = 0;
  int height = 0;
  int bitrate = 0;
  float framesPerSecond = 0.0f;
  std::atomic<bool> forceKeyFrame{true};
  std::uint64_t frameNumber = 0;
  std::vector<unsigned char> i420;

  ~Impl()
  {
    if (encoder) {
      encoder->Uninitialize();
      WelsDestroySVCEncoder(encoder);
    }
  }

  bool initialize(int requestedWidth,
                  int requestedHeight,
                  int requestedBitrate,
                  float requestedFramesPerSecond)
  {
    if (encoder && width == requestedWidth && height == requestedHeight &&
        bitrate == requestedBitrate && framesPerSecond == requestedFramesPerSecond) {
      return true;
    }
    if (encoder) {
      encoder->Uninitialize();
      WelsDestroySVCEncoder(encoder);
      encoder = nullptr;
    }
    if (WelsCreateSVCEncoder(&encoder) != 0 || !encoder) return false;
    int traceLevel = WELS_LOG_QUIET;
    encoder->SetOption(ENCODER_OPTION_TRACE_LEVEL, &traceLevel);

    SEncParamBase params{};
    params.iUsageType = SCREEN_CONTENT_REAL_TIME;
    params.iPicWidth = requestedWidth;
    params.iPicHeight = requestedHeight;
    params.iTargetBitrate = requestedBitrate;
    params.iRCMode = RC_BITRATE_MODE;
    params.fMaxFrameRate = requestedFramesPerSecond;
    if (encoder->Initialize(&params) != cmResultSuccess) {
      WelsDestroySVCEncoder(encoder);
      encoder = nullptr;
      return false;
    }
    int format = videoFormatI420;
    encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &format);
    width = requestedWidth;
    height = requestedHeight;
    bitrate = requestedBitrate;
    framesPerSecond = requestedFramesPerSecond;
    forceKeyFrame.store(true);
    frameNumber = 0;
    return true;
  }
#endif
};

RemoteVideoEncoder::RemoteVideoEncoder() : impl_(std::make_unique<Impl>()) {}
RemoteVideoEncoder::~RemoteVideoEncoder() = default;

bool RemoteVideoEncoder::available() const
{
#ifdef PARTICLE_VIS_HAVE_OPENH264
  return true;
#else
  return false;
#endif
}

RemoteVideoEncodeResult RemoteVideoEncoder::encodeRgba(
  int width,
  int height,
  const std::vector<unsigned char>& rgba,
  int bitrate,
  float framesPerSecond,
  RemoteVideoPacket& output)
{
#ifdef PARTICLE_VIS_HAVE_OPENH264
  output = {};
  if (width <= 0 || height <= 0 || (width & 1) || (height & 1) ||
      rgba.size() != static_cast<std::size_t>(width) * height * 4 ||
      !impl_->initialize(width, height, bitrate, framesPerSecond)) {
    return RemoteVideoEncodeResult::Failed;
  }
  RgbaToI420(width, height, rgba, impl_->i420);
  // A periodic recovery point limits the effect of a packet dropped by the
  // latest-frame remote transport without turning every frame into an IDR.
  if (impl_->frameNumber > 0 && impl_->frameNumber % 30 == 0) {
    impl_->forceKeyFrame.store(true);
  }
  if (impl_->forceKeyFrame.exchange(false)) {
    impl_->encoder->ForceIntraFrame(true);
  }

  SSourcePicture picture{};
  picture.iColorFormat = videoFormatI420;
  picture.iPicWidth = width;
  picture.iPicHeight = height;
  picture.iStride[0] = width;
  picture.iStride[1] = width / 2;
  picture.iStride[2] = width / 2;
  picture.pData[0] = impl_->i420.data();
  picture.pData[1] = picture.pData[0] + static_cast<std::size_t>(width) * height;
  picture.pData[2] = picture.pData[1] + static_cast<std::size_t>(width) * height / 4;
  picture.uiTimeStamp = static_cast<long long>(
    impl_->frameNumber++ * 1000.0 / std::max(impl_->framesPerSecond, 1.0f));

  SFrameBSInfo info{};
  if (impl_->encoder->EncodeFrame(&picture, &info) != cmResultSuccess) {
    return RemoteVideoEncodeResult::Failed;
  }
  if (info.eFrameType == videoFrameTypeSkip) {
    return RemoteVideoEncodeResult::Skipped;
  }
  output.width = width;
  output.height = height;
  output.keyFrame = info.eFrameType == videoFrameTypeIDR ||
                    info.eFrameType == videoFrameTypeI;
  for (int layer = 0; layer < info.iLayerNum; ++layer) {
    const SLayerBSInfo& layerInfo = info.sLayerInfo[layer];
    int layerBytes = 0;
    for (int nal = 0; nal < layerInfo.iNalCount; ++nal) {
      layerBytes += layerInfo.pNalLengthInByte[nal];
    }
    output.bytes.insert(output.bytes.end(),
                        layerInfo.pBsBuf,
                        layerInfo.pBsBuf + layerBytes);
  }
  return output.bytes.empty()
           ? RemoteVideoEncodeResult::Failed
           : RemoteVideoEncodeResult::Encoded;
#else
  (void)width; (void)height; (void)rgba; (void)bitrate;
  (void)framesPerSecond; (void)output;
  return RemoteVideoEncodeResult::Failed;
#endif
}

void RemoteVideoEncoder::requestKeyFrame()
{
#ifdef PARTICLE_VIS_HAVE_OPENH264
  impl_->forceKeyFrame.store(true);
#endif
}

struct RemoteVideoDecoder::Impl {
#ifdef PARTICLE_VIS_HAVE_OPENH264
  ISVCDecoder* decoder = nullptr;
  ~Impl()
  {
    if (decoder) {
      decoder->Uninitialize();
      WelsDestroyDecoder(decoder);
    }
  }

  bool initialize()
  {
    if (decoder) return true;
    if (WelsCreateDecoder(&decoder) != 0 || !decoder) return false;
    int traceLevel = WELS_LOG_QUIET;
    decoder->SetOption(DECODER_OPTION_TRACE_LEVEL, &traceLevel);
    SDecodingParam params{};
    params.uiTargetDqLayer = static_cast<unsigned char>(-1);
    params.eEcActiveIdc = ERROR_CON_SLICE_COPY;
    params.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;
    if (decoder->Initialize(&params) != cmResultSuccess) {
      WelsDestroyDecoder(decoder);
      decoder = nullptr;
      return false;
    }
    return true;
  }
#endif
};

RemoteVideoDecoder::RemoteVideoDecoder() : impl_(std::make_unique<Impl>()) {}
RemoteVideoDecoder::~RemoteVideoDecoder() = default;

bool RemoteVideoDecoder::available() const
{
#ifdef PARTICLE_VIS_HAVE_OPENH264
  return true;
#else
  return false;
#endif
}

bool RemoteVideoDecoder::decode(const unsigned char* data,
                                std::size_t size,
                                int expectedWidth,
                                int expectedHeight,
                                std::vector<unsigned char>& rgba)
{
#ifdef PARTICLE_VIS_HAVE_OPENH264
  if (!data || size == 0 || !impl_->initialize()) return false;
  unsigned char* planes[3] = {nullptr, nullptr, nullptr};
  SBufferInfo info{};
  const DECODING_STATE state = impl_->decoder->DecodeFrameNoDelay(
    data, static_cast<int>(size), planes, &info);
  if (state != dsErrorFree || info.iBufferStatus != 1) return false;
  const int width = info.UsrData.sSystemBuffer.iWidth;
  const int height = info.UsrData.sSystemBuffer.iHeight;
  if (width != expectedWidth || height != expectedHeight) return false;
  I420ToRgba(planes[0], planes[1], planes[2], width, height,
             info.UsrData.sSystemBuffer.iStride[0],
             info.UsrData.sSystemBuffer.iStride[1], rgba);
  return true;
#else
  (void)data; (void)size; (void)expectedWidth; (void)expectedHeight; (void)rgba;
  return false;
#endif
}
