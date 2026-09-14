# Linux GPU remote validation

This procedure validates the raw remote path before compression or live
simulation transport is introduced.

## 1. Build on Linux

On Freya, load the compiler and library modules, then prepare the dependencies
that are not provided as modules:

```bash
module purge
module load gcc/14 cmake/4.0 hdf5-serial/1.14.1 fftw-serial/3.3.10
./scripts/bootstrap_optional_submodules.sh \
  glm eigen nlohmann_json libzmq cppzmq
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

The CPU-only protocol tests do not require a GPU allocation. The expected
result is four passing tests.

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
./scripts/launch_particle_vis.sh remote
```

This selects `build-headless-local/particle_vis`, defaults EGL to the surfaceless
platform, and binds the frame and input sockets to `127.0.0.1:5560` and
`127.0.0.1:5561`. Set `PARTICLE_VIS_CONFIG_PATH` before the command when using a
separate server-side snapshot configuration.

The server log should identify the EGL/OpenGL renderer and show a successfully
loaded snapshot.

## 4. Open the SSH tunnel from the Mac

In a Mac terminal, connect directly to the allocated node through the login
host. Replace both placeholders with the cluster-specific values:

```bash
ssh -N \
  -L 5560:127.0.0.1:5560 \
  -L 5561:127.0.0.1:5561 \
  -J LOGIN_ALIAS GPU_NODE
```

The final SSH destination must be the node running `particle_vis`; this keeps
the ZeroMQ sockets bound to loopback on that node.

Freya currently does not allow a direct SSH login to an allocated compute node.
For a short interactive test, bind only to the cluster network on different
ports when starting the server:

```bash
PARTICLE_VIS_REMOTE_FRAME_ENDPOINT=tcp://0.0.0.0:5570 \
PARTICLE_VIS_REMOTE_INPUT_ENDPOINT=tcp://0.0.0.0:5571 \
./scripts/launch_particle_vis.sh remote
```

Then let the login node relay directly to the allocated hostname:

```bash
ssh -N \
  -L 5570:GPU_NODE:5570 \
  -L 5571:GPU_NODE:5571 \
  freya
```

Use this cluster-interface binding only for the lifetime of the allocation,
and stop the server when testing is complete.

## 5. Start the Mac viewer

```bash
./build/remote_frame_viewer \
  tcp://127.0.0.1:5570 \
  tcp://127.0.0.1:5571
```

Verify particle rendering, Retina font size, rotation, pan, zoom, text input,
resize, the embedded server-side file browser, focus loss, and the two-step
Escape behavior from a maximized window.

The viewer prints received physical resolution, logical display size, DPI
scale, and RGBA byte count. Record these together with observed interaction
latency. Raw 2560×1440 RGBA frames are about 14 MiB each, so this test is also a
baseline for deciding the next compression and pacing work.
