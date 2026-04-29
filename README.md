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
