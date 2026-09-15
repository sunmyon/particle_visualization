# Linux GPU remote validation

This procedure validates the demand-driven H.264 remote path and interactive
input transport. Live simulation transport is outside this test.

## 1. Build on Linux

On Freya, load the compiler and library modules, then prepare the dependencies
that are not provided as modules:

```bash
module purge
module load gcc/14 cmake/4.0 hdf5-serial/1.14.1 fftw-serial/3.3.10
./scripts/bootstrap_optional_submodules.sh \
  glm eigen nlohmann_json libzmq cppzmq openh264
```

The bootstrap installs only inside `external/submodules/_install`. Then build
on the login or GPU node:

```bash
git fetch origin work/codex
git switch work/codex
cmake --preset linux-headless-gcc
cmake --build --preset linux-headless-gcc
ctest --test-dir build-headless-local --output-on-failure
```

The configure output should report `EGL headless context support: ON` and keep
`PYTHON_BRIDGE` enabled. If the bridge is automatically disabled, install or
load ZeroMQ, cppzmq, and nlohmann-json before continuing.

The CPU-only protocol and codec tests do not require a GPU allocation. The
expected result is six passing tests when OpenH264 is enabled.

For a complete automated GPU loopback on Freya, load the current Python stack
and run:

```bash
module load python-waterboa/2025.06
srun -J pv-loopback --partition=p.gpu.ampere --nodes=1 \
  --constraint=gpu --gres=gpu:a100:1 --ntasks=1 --cpus-per-task=2 \
  --time=00:05:00 bash -c \
  'EGL_PLATFORM=surfaceless python3 scripts/test_remote_loopback.py \
    --executable build-headless-local/particle_vis \
    --width 480 --height 320 --resize 1280 720 \
    --display-scale 2 --timeout 15'
```

## 2. Allocate a GPU node

Use the cluster scheduler to enter an interactive GPU allocation. On Freya, the
existing project documentation uses:

```bash
srun -J vis --partition=p.gpu.ampere --nodes=1 --constraint="gpu" \
     --gres=gpu:a100:1 --ntasks-per-node=1 --cpus-per-task=1 \
     --time=23:59:59 --pty bash -i
```

Record the allocated node hostname:

```bash
hostname -f
```

## 3. Start the server

From the repository on the allocated node:

```bash
PARTICLE_VIS_REMOTE_MAX_FPS=5 ./scripts/launch_particle_vis.sh remote
```

This selects `build-headless-local/particle_vis`, defaults EGL to the surfaceless
platform, and binds the frame and input sockets to `127.0.0.1:5560` and
`127.0.0.1:5561`. Set `PARTICLE_VIS_CONFIG_PATH` before the command when using a
separate server-side snapshot configuration.

The server log should identify the EGL/OpenGL renderer and show a successfully
loaded snapshot. When OpenH264 was bootstrapped, remote frames use a persistent
H.264 stream at 5 Mbit/s by default. Change the target with
`PARTICLE_VIS_REMOTE_VIDEO_BITRATE`. Set `PARTICLE_VIS_REMOTE_CODEC=jpeg` to
use the previous JPEG path; `PARTICLE_VIS_REMOTE_JPEG_QUALITY` then selects a
quality from 1 to 100, and zero selects raw RGBA.

## 4. Start the loopback-only Slurm relay on the Mac

Keep the server on its default compute-node loopback endpoints. Do not bind the
server to `0.0.0.0`. In another Freya login shell, record the allocation's job
ID with `squeue -u $USER`; also keep the short node name from `hostname`.

Freya does not allow a direct SSH login to an allocated compute node. Run the
relay locally on the Mac instead. It binds only Mac loopback, opens SSH to the
login node, and uses overlapping Slurm job steps to reach the two compute-node
loopback sockets:

```bash
python3 scripts/slurm_loopback_relay.py relay \
  --login freya \
  --job-id JOB_ID \
  --node GPU_NODE
```

For example:

```bash
python3 scripts/slurm_loopback_relay.py relay \
  --login freya \
  --job-id 1089140 \
  --node freyag204
```

The relay listens only on `127.0.0.1:5570` and `127.0.0.1:5571`. Each accepted
connection is carried over SSH standard input/output and an `srun --overlap`
step to `127.0.0.1:5560` or `127.0.0.1:5561` on the allocated node.

## 5. Start the Mac viewer

```bash
./build/remote_frame_viewer \
  tcp://127.0.0.1:5570 \
  tcp://127.0.0.1:5571
```

During interaction, the viewer requests 0.75 server-rendered pixels per logical
window pixel by default. This avoids rendering at the full Retina framebuffer
resolution while keeping the native window large. After 1500 ms without input,
it requests one full-resolution Retina frame. A held mouse button or key keeps
the viewer in interactive mode. Adjust the interactive resolution
without changing the window size with:

```bash
PARTICLE_VIS_VIEWER_RENDER_SCALE=0.75 \
./build/remote_frame_viewer \
  tcp://127.0.0.1:5570 \
  tcp://127.0.0.1:5571
```

Use `1` for full logical interaction resolution or values below `1` for lower
latency. `PARTICLE_VIS_VIEWER_IDLE_RENDER_SCALE` controls the final idle
frame and defaults to `2`; `PARTICLE_VIS_VIEWER_IDLE_DELAY_MS` controls its
delay. Set the idle scale to `1` to disable the Retina-quality final frame.

The viewer also forwards local clipboard text to the remote UI when pressing
Command+V on macOS or Ctrl+V on other platforms.

To compare bandwidth at a smaller viewer size:

```bash
PARTICLE_VIS_VIEWER_WIDTH=640 PARTICLE_VIS_VIEWER_HEIGHT=360 \
./build/remote_frame_viewer \
  tcp://127.0.0.1:5570 \
  tcp://127.0.0.1:5571
```

Verify particle rendering, Retina font size, rotation, pan, zoom, text input,
resize, the embedded server-side file browser, focus loss, and the two-step
Escape behavior from a maximized window.

The viewer requests a frame after consuming the previous one. The server sends
only after valid remote input or resize changes the scene; idle sessions send
no repeated frame data, and input bursts collapse into one pending frame. Valid
input can replace an in-flight frame directly, so interaction does not wait for
the previous frame's receive acknowledgement to return to the server. It
prints physical resolution, logical display size, DPI scale, and decoded RGBA
size. It also reports server readback latency and copy time, encoder queue and
encode time, plus client decode and texture upload time. Record these values
together with observed interaction latency. The EGL/OpenGL server uses a
double-buffered PBO readback, and encoding runs on a worker thread. Use
`PARTICLE_VIS_REMOTE_CODEC=jpeg` for a direct comparison with the old transport.
