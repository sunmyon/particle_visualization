# particle_visualization

## Data Model / データモデル

The core data model treats particles and mesh cells as spatial samples. Mesh
connectivity is intentionally not part of the common model; cell-centered mesh
data can be analyzed and rendered through position, support radius, and field
arrays.

現在の core data model は、粒子と mesh cell を「空間サンプル」として共通に
扱います。mesh connectivity は共通モデルには含めず、cell center・代表半径・
field array によって近似的な解析/描画を行います。

This keeps the common path lightweight for particle data, SPH-like data, and
cell-centered mesh outputs such as Voronoi/Arepo-style snapshots. Explicit mesh
topology should remain optional and feature-specific, for example for a future
structured-grid or exact mesh-surface tool.

この方針により、粒子データ、SPH 的なデータ、Voronoi/Arepo 形式の cell
centered output を同じ軽量な経路で扱えます。明示的な mesh topology は、将来
structured grid や厳密な mesh surface 表示が必要になった場合の optional な
機能として扱います。

## Clone

```bash
git clone --recurse-submodules <repo_url>
git submodule update --init --recursive
```

## Dependencies

### macOS

```bash
brew install glfw
brew install glm
brew install hdf5
```

### Ubuntu

```bash
sudo apt install -y libglfw3 libglfw3-dev libglm-dev libhdf5-dev freeglut3-dev mesa-common-dev libglu1-mesa-dev
```

## Linux Headless Preset

After loading your compiler and MPI modules, configure with the preset:

```bash
cmake --preset linux-headless-gcc
cmake --build --preset linux-headless-gcc
```

## Linux GUI Presets

Three GUI presets are available, covering different display-server scenarios:

| Preset | Display backend | When to use |
| ------ | --------------- | ----------- |
| `linux-gui-gcc` | None (null / offscreen) | Portable fallback — compiles anywhere, no native window |
| `linux-gui-wayland` | Wayland | GPU node or workstation with Wayland compositor |
| `linux-gui-x11` | X11 | System with X11 dev packages (`libxrandr`, `libxinerama`, etc.) |

### Accessing a GPU Node (MPCDF Freya)

```bash
srun -J vis --partition=p.gpu.ampere --nodes=1 --constraint="gpu" \
     --gres=gpu:a100:1 --ntasks-per-node=1 --cpus-per-task=1 \
     --time=23:59:59 --pty bash -i
```

After the shell opens, load the usual modules:

```bash
module load gcc/11 cuda/11.6 openmpi_gpu/4.1 cmake/4.0
```

### Data Setup (Default Snapshot)

The app defaults to reading `./example/output_0000.dat`.
Prepare that file with:

```bash
bash ./example/download_data.sh
```

What this does:

1. Attempts to download `output_0000.dat` from known release URLs.
2. If URLs are unavailable, generates a synthetic binary snapshot in the exact record layout expected by the default binary reader.
3. Writes both `example/data/output_0000.dat` and `example/output_0000.dat`.

You can override the primary URL with:

```bash
PARTICLE_VIS_SAMPLE_URL="https://your-host/path/output_0000.dat" bash ./example/download_data.sh
```

### Launch Wrapper (Recommended on Freya)

Use the wrapper to launch with runtime-safe library paths and automatic data setup:

```bash
./scripts/launch_particle_vis.sh [auto|gui|headless]
```

Behavior:

1. Ensures `example/output_0000.dat` exists (calls `example/download_data.sh` when missing).
2. Selects binary by mode:
   - `gui`: `./particle_vis`
   - `headless`: `./build-headless-local/particle_vis`
   - `auto`: GUI only when `DISPLAY` is reachable (`xdpyinfo`/`xset` probe), otherwise headless
3. Sets `LD_LIBRARY_PATH=/usr/lib64:/lib64` to avoid Mesa/GLIBCXX mismatch on cluster module stacks.
4. In headless mode, defaults `PARTICLE_VIS_EGL_PLATFORM=surfaceless`.

If you see:

```text
GLFW error 65550: X11: Failed to open display :1
```

run with explicit headless mode:

```bash
./scripts/launch_particle_vis.sh headless
```

or start an interactive session with working X11 forwarding before using `gui` mode.

### Wayland Backend (Recommended on GPU Nodes)

Bootstrap the Wayland stack and rebuild GLFW with Wayland support in one step:

```bash
./scripts/bootstrap_optional_submodules.sh --with-wayland glfw
```

This will:

1. Clone `wayland`, `wayland-protocols`, and `xkbcommon` as submodules (via `gitlab.freedesktop.org` / `github.com/xkbcommon`).
2. Install `meson` and `ninja` via `pip --user` if not already present.
3. Build and install the Wayland stack under `external/submodules/_install/{wayland,wayland-protocols,xkbcommon}/`.
4. Rebuild GLFW with `GLFW_BUILD_WAYLAND=ON`, pointing `PKG_CONFIG_PATH` at the local installs.

Then configure and build:

```bash
cmake --preset linux-gui-wayland
cmake --build --preset linux-gui-wayland
```

The preset automatically adds the local install prefixes to `CMAKE_PREFIX_PATH`.

### X11 Backend (Default — GPU Nodes and Workstations with `DISPLAY`)

X11 is the default backend built by the bootstrap script. Because the GPU node has the X11 runtime (`libX11`, `libXinerama`, `libXrandr`, `libXcursor`, `libXi`) but some `-devel` header packages are absent, the bootstrap provides minimal compile-time stub headers under `external/submodules/_x11_stubs/`. All missing symbols are loaded at runtime via `dlopen`/`dlsym` by GLFW, so no runtime dependency is added.

Bootstrap GLFW (X11 backend, works without extra system packages):

```bash
./scripts/bootstrap_optional_submodules.sh glfw
```

Then build:

```bash
cmake --preset linux-gui-x11
cmake --build --preset linux-gui-x11
```

Run:

```bash
./scripts/launch_particle_vis.sh gui
```

> **Tip:** A non-empty `DISPLAY` is not always usable inside batch jobs. Prefer `./scripts/launch_particle_vis.sh auto` (fallback built in) or `headless` for non-interactive runs.

### Null / Portable Backend

For nodes without any display-server dev packages, the portable null-backend build still compiles fully:

```bash
cmake --preset linux-gui-gcc
cmake --build --preset linux-gui-gcc
```

For a headless runtime test on cluster nodes:

```bash
cmake --preset linux-headless-gcc
cmake --build --preset linux-headless-gcc
./scripts/launch_particle_vis.sh headless
```

## Optional Dependency Submodules

The project can prefer optional dependencies from `external/submodules/` before system packages.
Use the bootstrap script to add and optionally build local submodule-based dependencies:

```bash
./scripts/bootstrap_optional_submodules.sh
```

To also build heavier CMake dependencies such as HDF5 and VTK into `external/submodules/_install/`:

```bash
./scripts/bootstrap_optional_submodules.sh --with-heavy
```

For GUI readiness on this Linux system, bootstrap GLFW (builds with X11 backend by default):

```bash
./scripts/bootstrap_optional_submodules.sh glfw
```

For Wayland-enabled GLFW (GPU node or Wayland-capable system):

```bash
./scripts/bootstrap_optional_submodules.sh --with-wayland glfw
```

The default X11 backend works on any node with `DISPLAY` set, using local stub headers for the handful of missing `-devel` packages (`Xinerama`, `XInput2`, `Xkb`). All symbols are resolved at runtime via `dlopen`.
Use `--with-wayland` to build the Wayland backend instead, for use with the `linux-gui-wayland` preset.

### Tested Bootstrap Workflow (Linux)

The following command was validated in this repository to build and install local `gmp`, `mpfr`, `lua`, and `cgal` under `external/submodules/_install/`:

```bash
./scripts/bootstrap_optional_submodules.sh --with-heavy gmp mpfr lua cgal
```

Installed outputs are expected in:

```text
external/submodules/_install/gmp
external/submodules/_install/mpfr
external/submodules/_install/lua
external/submodules/_install/cgal
```

Notes:

- MPFR may print an `install-info` warning for `mpfr.info` on systems without texinfo docs generation. This is non-fatal in the current workflow.
- Lua from the git submodule is installed from its upstream `makefile` build products (no upstream `make install` target).

After bootstrapping, validate configure/build:

```bash
cmake --preset linux-headless-gcc
cmake --build build-headless-local -j "$(nproc)"
```

For GUI preset validation:

```bash
cmake --preset linux-gui-gcc
cmake --build --preset linux-gui-gcc
```

> **Tip:** When switching between presets or compiler modules, clear the preset's CMake cache directory first:
>
> ```bash
> rm -rf build-gui-local/CMakeCache.txt build-gui-local/CMakeFiles
> # or for Wayland:
> rm -rf build-gui-wayland/CMakeCache.txt build-gui-wayland/CMakeFiles
> ```

## Windows Build Notes

Note: You can compile inside WSL2, but GPU acceleration in WSL2 may cause issues.
For best results, build natively on Windows with Visual Studio.

### 1. Install prerequisites

1. Visual Studio 2022 (Community or higher)
   - Select workload: Desktop development with C++
   - Optional: C++ CMake tools for Windows
   - Optional: Windows 10/11 SDK
2. CMake (optional if using Visual Studio's bundled CMake)

```powershell
winget install Kitware.CMake
```

### 2. Open developer environment

- Recommended: Developer PowerShell for VS 2022
- Also possible: x64 Native Tools Command Prompt for VS 2022 (not fully tested)

### 3. Configure

List available presets:

```bash
cmake --list-presets
```

If available, use one of these presets (linked with vcpkg):

```bash
cmake --preset windows-msvc-vcpkgroot
cmake --preset windows-msvc-vctools
```

### 4. Build

```bash
cmake --build build --config Release
```

The executable will be generated at:

```text
build/Release/particle_vis.exe
```
