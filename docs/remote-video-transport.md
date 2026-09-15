# Remote video transport

## Status

The remote viewer uses a persistent H.264 stream for interactive frames when
OpenH264 is available. The final high-resolution idle frame is an independent
JPEG, so it does not recreate the interactive codec state. Raw RGBA remains a
diagnostic and compatibility path. This transport covers the rendered
application framebuffer, including ImGui. Local ImGui rendering and live
simulation-data transport are separate future work.

## Pipeline

```text
particle_vis on the GPU node
  render scene + ImGui
    -> asynchronous framebuffer readback
    -> one-slot latest-frame queue
    -> interactive worker: RGBA to I420 and OpenH264 encode
    -> idle worker: independent JPEG encode
    -> ZeroMQ multipart message over the loopback-only Slurm relay
    -> Mac viewer: OpenH264 decode, I420 to RGBA, OpenGL texture upload
    -> local window

Mac input
    -> ZeroMQ input channel
    -> RemoteInputReceiver
    -> application input queue
```

Rendering remains demand-driven. A valid input, resize, application content
change, or viewer frame request marks the scene dirty. Repeated changes can
replace a raw frame that has not entered the encoder, avoiding an unbounded
encode queue. H.264 frames that have already been encoded are kept in order
because later P-frames can refer to them.

Interactive and idle encoding use separate workers. New interactive work drops
an idle frame that has not started encoding, prioritizes completed interactive
output, and rejects an idle JPEG that finishes after a newer frame was queued.
An idle JPEG already inside the JPEG library may finish in the background, but
it does not hold the H.264 queue.

## Codec selection

Build OpenH264 on both the server and viewer machines:

```bash
git submodule update --init external/submodules/openh264
./scripts/bootstrap_optional_submodules.sh openh264
```

CMake enables H.264 automatically when it finds the installed headers and
library under `external/submodules/_install/openh264`. A build without that
dependency continues to compile and uses JPEG. On x86 systems without `nasm`,
the bootstrap builds a portable OpenH264 library without assembly.

Runtime selection:

| Setting | Meaning | Default |
|---|---|---:|
| `PARTICLE_VIS_REMOTE_CODEC=h264` | Prefer persistent H.264; fall back per frame if encoding is unavailable | selected when OpenH264 is built |
| `PARTICLE_VIS_REMOTE_CODEC=jpeg` | Use independent JPEG frames | off |
| `PARTICLE_VIS_REMOTE_VIDEO_BITRATE` | OpenH264 target bitrate in bit/s | `5000000` |
| `PARTICLE_VIS_REMOTE_MAX_FPS` | Maximum server frame rate; zero disables pacing | `10` |
| `PARTICLE_VIS_REMOTE_JPEG_QUALITY` | JPEG fallback quality; zero selects raw RGBA | `80` |

The video bitrate is a rate-control target, not a maximum packet size. An IDR
frame can be much larger than one frame's share of the target bitrate. Idle
JPEG size is controlled by `PARTICLE_VIS_REMOTE_JPEG_QUALITY` instead.

## Frame protocol

Each frame is one ZeroMQ multipart message:

1. UTF-8 JSON header
2. encoded payload

H.264 headers use:

```json
{
  "type": "h264_frame",
  "format": "H264_ANNEX_B",
  "frameId": 42,
  "width": 960,
  "height": 540,
  "displayWidth": 1280,
  "displayHeight": 720,
  "framebufferScaleX": 0.75,
  "framebufferScaleY": 0.75,
  "presentationMode": "interactive",
  "keyFrame": false,
  "bytes": 24000,
  "rawBytes": 2073600
}
```

The header also carries server readback, readback-latency, encoder-queue, and
encode timings. Remote input frames carry the client input sequence that
triggered them and the server time from receiving that input to submitting the
readback. The viewer adds decode and texture-upload timings to its log.
JPEG (`jpeg_frame`) and raw RGBA (`rgba_frame`) use the same display and timing
metadata. `presentationMode` is `interactive` for the persistent video stream
and `idle` for the final high-resolution still frame.

OpenH264 receives even-sized I420 frames, so the viewer rounds each requested
interactive framebuffer dimension down by at most one pixel. An interactive
resolution change recreates the encoder and produces an IDR frame. Switching to
an idle JPEG at another resolution leaves that encoder untouched. The encoder
also produces a recovery IDR every 30 encoded frames to limit damage from a
lost inter-frame packet.

## Resolution policy

The Mac window size and server render size are independent:

- During input, `PARTICLE_VIS_VIEWER_RENDER_SCALE` defaults to `0.75`.
- After 1500 ms without input, the viewer requests one independent JPEG using
  `PARTICLE_VIS_VIEWER_IDLE_RENDER_SCALE`, which defaults to `2.0` for Retina.
- `PARTICLE_VIS_VIEWER_IDLE_DELAY_MS` changes the idle delay.

This keeps the window large while reducing interactive encode and transfer
cost. The full-resolution idle frame is intentionally expensive but is sent
only after interaction stops. Returning to interaction resumes the existing
H.264 stream without forcing a new IDR solely because an idle frame was shown.

## Latency diagnostics

Set `PARTICLE_VIS_VIEWER_LOG_EVERY_N_FRAMES=1` on the viewer to log every
displayed frame. The additional fields are:

| Field | Interval |
|---|---|
| `trigger to readback` | server input receipt to framebuffer-readback submission |
| `readback latency` | readback submission to CPU-side result collection |
| `encode queue` | CPU frame waiting for its encoder worker |
| `encode` | server compression |
| `input to receive` | Mac input send to completion of the corresponding frame receive |
| `decode` | Mac decompression and color conversion |
| `upload` | decoded RGBA upload call into the local OpenGL texture |
| `input to display` | input send through completion of the texture upload call |

`input to receive` uses the Mac monotonic clock for both endpoints, so it does
not depend on clock synchronization with the server. `transport/unmeasured` is
the input-to-receive interval minus the measured server stages. It includes
both network directions, SSH/Slurm relay work, and any server work not covered
by the named stages; it must not be interpreted as pure network time.

## Validation baseline

The implementation is covered by `remote_video_codec_test`, which encodes and
decodes an IDR frame followed by a P-frame. Existing input, JPEG, and local
backend tests also run unchanged. The automated loopback test exercises the
real renderer, resize, remote input, the idle JPEG transition, H.264
continuation without a new IDR, and remote Escape exit.

One Freya A100 to Mac session on 2026-09-15, before the independent idle-JPEG
path was added, produced the following diagnostic samples. They are reference
observations rather than a throughput benchmark:

| State | Render size | Payload | Server encode | Mac decode |
|---|---:|---:|---:|---:|
| Interactive | 960 x 540 | 23-28 KB | 28-32 ms | about 5 ms |
| Maximized interactive | 1134 x 712 | about 53 KB | about 57 ms | about 7 ms |
| Full Retina idle IDR (previous path) | 3024 x 1898 | about 1.28 MB | about 333 ms | about 81 ms |

Normal encoder-queue samples were approximately 0.05-0.08 ms, with no sustained
queue growth observed in that session. Measure payload rate and interaction
latency over a longer camera gesture before changing bitrate, frame pacing, or
render scale.

## Current limits

- RGBA-to-I420 and I420-to-RGBA conversion currently run on the CPU.
- The Freya build is portable when `nasm` is absent, so it does not use
  OpenH264's x86 assembly path.
- The transport uses ZeroMQ PUB/SUB. A periodic IDR provides recovery, but the
  protocol does not yet request a keyframe immediately after decoder loss.
- Changing the interactive render resolution still recreates codec state and
  sends an IDR frame. The idle Retina JPEG does not.
- ImGui is part of the encoded framebuffer. Sending ImGui draw data for local
  composition is not part of this milestone.

These measurements should guide the next optimization. Likely candidates are
faster color conversion, enabling the OpenH264 assembly path on Freya, and
explicit decoder-loss recovery. Local ImGui composition should be evaluated
for text clarity and independent UI resolution after the video path has a
sustained-bandwidth baseline.
