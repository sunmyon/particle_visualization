# Quick Start

This guide gets a fresh checkout to the first rendered snapshot. The default
sample path is `example/output_0000.dat`; `example/download_data.sh` prepares
that file and also fetches the optional Gadget-format sample
`example/ics_gadget.dat` when it is available.

## macOS

### 1. Install dependencies

Use Homebrew for the normal macOS build:

```bash
brew install cmake pkg-config
brew install glfw glm hdf5
brew install zeromq cppzmq nlohmann-json eigen fftw
```

Metal support uses the Apple system SDK. Vulkan projection/render experiments
also need MoltenVK:

```bash
brew install vulkan-headers vulkan-loader vulkan-tools molten-vk
```

### 2. Clone and build

```bash
git clone --recurse-submodules https://github.com/sunmyon/particle_visualization.git
cd particle_visualization
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### 3. Prepare test data

```bash
bash ./example/download_data.sh
```

This writes the app-facing sample files under `example/` and keeps downloaded
copies under `example/data/`.

### 4. Run

```bash
./build/particle_vis
```

The app loads `example/output_0000.dat` at startup.

### 5. Load another file

In the Settings window, open `File Navigation`.

1. Click `Browse Files`.
2. Select a snapshot file, for example `example/ics_gadget.dat`.
3. Set `Data format` if auto-detection is not enough. Use `Gadget` for
   `ics_gadget.dat`, `HDF5` for `.hdf5` / `.h5`, and `Binary` for the generated
   default sample.
4. Click `Reload`.

## Linux Workstation

### 1. Install dependencies

On Ubuntu-like systems:

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config \
  libglfw3 libglfw3-dev libglm-dev libhdf5-dev \
  libzmq3-dev cppzmq-dev nlohmann-json3-dev \
  libeigen3-dev libfftw3-dev \
  freeglut3-dev mesa-common-dev libglu1-mesa-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
```

### 2. Clone and build

For an X11 GUI build:

```bash
git clone --recurse-submodules https://github.com/sunmyon/particle_visualization.git
cd particle_visualization
cmake --preset linux-gui-x11
cmake --build --preset linux-gui-x11
```

If your Linux system does not have the full X11 development stack, bootstrap the
local GLFW fallback first:

```bash
./scripts/bootstrap_optional_submodules.sh glfw
cmake --preset linux-gui-x11
cmake --build --preset linux-gui-x11
```

### 3. Prepare test data

```bash
bash ./example/download_data.sh
```

### 4. Run

```bash
./build-gui-x11/particle_vis
```

Then use the same `File Navigation` flow described in the macOS section:
`Browse Files`, choose the file, select the format if needed, and click
`Reload`.

## Linux Headless or Cluster

Use this path when no interactive display is available or when running on a
remote GPU node.

### 1. Load modules or install packages

Use your site modules when available, for example:

```bash
module load gcc/11 cuda/11.6 openmpi_gpu/4.1 cmake/4.0
```

On package-managed systems, install the Linux workstation dependencies above.

### 2. Build

```bash
cmake --preset linux-headless-gcc
cmake --build --preset linux-headless-gcc
```

### 3. Prepare test data and run

```bash
bash ./example/download_data.sh
./scripts/launch_particle_vis.sh headless
```

The launch wrapper prepares missing default data, chooses a GUI or headless
binary in `auto` mode, and sets the EGL surfaceless runtime setting for
headless runs.

## Troubleshooting

- If `Browse Files` is unavailable, rebuild without `NONATIVEFILEDIALOG=ON` or
  use the bundled ImGui file dialog path.
- If the default sample download fails, `download_data.py` generates a small
  synthetic binary snapshot automatically.
- If `ics_gadget.dat` is unavailable from the direct release URL in a private
  repository checkout, authenticate with `gh auth login`; the script can fall
  back to `gh release download`.
- If CMake keeps stale dependency paths after switching presets or modules,
  remove that build directory and configure again.
