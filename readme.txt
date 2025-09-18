git clone --recurse-submodules <repo_url>
git submodule update --init --recursive　

For Mac users:
brew install glfw
brew install glm
brew install hdf5

For Ubuntu users:
apt install -y libglfw3 libglfw3-dev libglm-dev libhdf5-dev freeglut3-dev mesa-common-dev libglu1-mesa-dev

For Windows users:
⚠️ Note: You can compile the code inside WSL2 (Windows Subsystem for Linux). However, enabling GPU acceleration inside WSL2 may cause problems or unexpected behavior.
✅ For best results, it is recommended to build the project on native Windows with Visual Studio.

1. Install prerequisites
	A.	Visual Studio 2022 (Community or higher)
	•	During installation, make sure to select the following workloads:
	•	Desktop development with C++
	•	(Optional but recommended) C++ CMake tools for Windows
	•	(Optional) Windows 10/11 SDK (usually selected automatically)
　B.	CMake (may not be mandatory)
	•	Visual Studio ships with CMake support, but you can also install standalone CMake via cmake.org or winget:
　  winget install Kitware.CMake

2. Open the Developer environment
	•	Always open  "Developer PowerShell for VS 2022”.
  • “x64 Native Tools Command Prompt for VS 2022” may work, but is not tested.
This ensures the correct compiler and environment variables are available.

3. Configure the project
From the project root directory, you can check available presets by
cmake --list-presets
If one of the following presets is shown, cmake has automatically linked to vcpkg directory
cmake --preset windows-msvc-vcpkgroot
cmake --preset windows-msvc-vctools

4. Build
cmake --build build --config Release
This will produce the executable under:
build/Release/particle_vis.exe
