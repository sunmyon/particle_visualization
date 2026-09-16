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
    -> viewer receive worker: OpenH264 decode, I420 to RGBA
    -> latest completed video/still slots
    -> viewer main thread: OpenGL texture upload
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

The viewer owns its receive sockets and decoders on a separate worker thread.
Network waits and decoding no longer block GLFW input handling. Every received
H.264 packet is decoded in order, even when the UI skips presenting intermediate
images. Only the latest completed video and latest completed still are retained.
The main thread checks dimensions/generation, sends cumulative receipts for both
channels (including images discarded for display), and uploads selected images.
GLFW, OpenGL and the input socket remain exclusively on the main thread. The
receive worker uses a bounded poll wait and joins on exit. Launch commands and
the wire protocol are unchanged; this improvement needs only a viewer rebuild.
The `viewer wait` timing covers decoded-image readiness to texture upload start;
`input to display` includes that interval rather than just summing decode/upload.

## Codec selection

### Optional SIMD color conversion

`libyuv` accelerates RGBA/I420 conversion on both the server and viewer without
changing the H.264 stream. The portable conversion remains available when the
library is absent or `PARTICLE_VIS_ENABLE_LIBYUV=OFF`. This is a CPU optimization;
GPU encoding and shader-based YUV display are not implemented by this change.

On macOS/Linux, install the pinned version with:

```bash
./scripts/bootstrap_remote_libyuv.sh
cmake -S . -B build
cmake --build build -j4 --target remote_frame_viewer
```

On Freya, run the same bootstrap, then configure/build the existing server build:

```bash
cmake -S . -B build-headless-local
cmake --build build-headless-local -j4 --target particle_vis
```

Reuse the existing configured build directory to preserve its headless settings.
CMake reports `Remote SIMD color conversion: ON` when selected. The bootstrap
installs only under `external/submodules/_install/libyuv`; no system installation
or GPU job is needed. Other platforms can provide libyuv through CMake's
`LIBYUV_INCLUDE_DIR` and `LIBYUV_LIBRARY`. Runtime launch options are unchanged.
SIMD rounding can differ slightly from the portable path; conversion tests check
channel order, padded decoder strides, non-aligned widths and bounded color error.

### OpenH264

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
| `PARTICLE_VIS_REMOTE_CODEC=h264` | Prefer persistent H.264; fall back to JPEG if encoding fails | selected when OpenH264 is built |
| `PARTICLE_VIS_REMOTE_CODEC=jpeg` | Use independent JPEG frames | off |
| `PARTICLE_VIS_REMOTE_VIDEO_BITRATE` | OpenH264 target bitrate in bit/s | `5000000` |
| `PARTICLE_VIS_REMOTE_MAX_FPS` | Maximum server frame rate; zero disables pacing | `10` |
| `PARTICLE_VIS_REMOTE_JPEG_QUALITY` | JPEG fallback quality; zero selects raw RGBA | `80` |

The video bitrate is a rate-control target, not a maximum packet size. An IDR
frame can be much larger than one frame's share of the target bitrate. Idle
JPEG size is controlled by `PARTICLE_VIS_REMOTE_JPEG_QUALITY` instead.
OpenH264 may intentionally skip an interactive frame to keep its bitrate
target. The presenter sends nothing for that frame and continues with the next
video frame. Treating a rate-control skip as an encoding failure and sending a
JPEG would bypass the bitrate limit and can create seconds of queued latency at
large window sizes.

## Bounded transport comparison

Set `PARTICLE_VIS_REMOTE_TRANSPORT=bounded` on both processes to select the
newest-state transport. It uses PUSH/PULL on the existing frame endpoint,
keeps at most two video frames reserved or awaiting a receive confirmation,
and uses a separate PUSH/PULL endpoint for idle JPEG stills (one outstanding
still). The still endpoint defaults to `tcp://127.0.0.1:5562` on the server
and `tcp://127.0.0.1:5572` on the viewer through the loopback relay. The
viewer sends a frame receipt when its main thread takes a decoded image;
receipts are cumulative within each channel. Input continues to update while
the frame window is full, and adjacent pointer moves collapse in the server's
input queue. A frame superseded before readback or encoding is skipped.
`cameraGeneration` uses the monotonically increasing remote input sequence.
It advances for camera controls and other state-changing inputs, so the
generation gap is a conservative measure of how far the displayed image is
behind the user's latest request. The viewer window also reports the age of
the displayed image from its triggering input, or from receipt when there is
no matching input timestamp.

The legacy PUB/SUB transport remains the default for comparison. Both modes
retain H.264 reference frames once encoded. The bounded mode limits committed
frames to two video packets and one still packet, even though ZeroMQ, SSH and
the kernel may buffer the bytes of those packets. It cannot cancel a still
image after its send was accepted, and both SSH connections still share the
physical network. If a connection is lost before its receipt arrives, the
bounded mode may stop sending until reconnected; restart the viewer and server
for this diagnostic build.

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
The extended header records monotonic server timestamps for input receipt,
camera update, render start/end, encode start/end and send attempt. The viewer
logs its own receive-complete and display timestamps. Server and viewer
monotonic timestamps have different clock origins; compare intervals within
one process, not their raw values across machines. Queue depth, outstanding
frame slots and failed nonblocking send attempts are also reported.
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
| `input to frame start` | server input receipt to start of the application frame; included in `trigger to readback` |
| `frame to render` | application frame start to scene rendering start |
| `render` | scene rendering call |
| `readback latency` | readback submission to CPU-side result collection |
| `encode queue` | CPU frame waiting for its encoder worker |
| `encode` | server compression |
| `encode to send` | compressed frame waiting until the server starts sending it |
| `previous send` | time spent in the server's previous ZeroMQ multipart send call |
| `receive payload` | Mac receive of the frame payload after its header arrives |
| `input to receive` | Mac input send to completion of the corresponding frame receive |
| `decode` | Mac decompression and color conversion |
| `upload` | decoded RGBA upload call into the local OpenGL texture |
| `input to display` | input send through completion of the texture upload call |

`input to receive` uses the Mac monotonic clock for both endpoints, so it does
not depend on clock synchronization with the server. `transport/unmeasured` is
the input-to-receive interval minus the measured server stages. It includes
both network directions, SSH/Slurm relay work, and any server work not covered
by the named stages; it must not be interpreted as pure network time.
The server drops obsolete unencoded work and stale idle JPEGs when newer input
arrives. Already encoded H.264 frames are retained because later P-frames may
depend on them. This avoids breaking decoder state while limiting work on old
input.

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
