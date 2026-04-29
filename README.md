# particle_visualization

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

If you are switching compiler modules, clear the old cache first:

```bash
rm -rf build-headless-local/CMakeCache.txt build-headless-local/CMakeFiles
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

Once those are installed, the `linux-headless-gcc` preset will pick them up automatically through CMake prefix paths.

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
