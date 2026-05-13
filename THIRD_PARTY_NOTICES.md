# Third-Party Notices

This project includes or vendors the following third-party components. Each
component remains under its own license. See the referenced files for full
license text.

| Component | Local path | License | Upstream |
| --- | --- | --- | --- |
| Dear ImGui | `external/imgui` | MIT | https://github.com/ocornut/imgui |
| ImPlot | `external/implot` | MIT | https://github.com/epezent/implot |
| ImGuiFileDialog | `external/ImGuiFileDialog` | MIT | https://github.com/aiekick/ImGuiFileDialog |
| GLM | `external/glm` | MIT or Happy Bunny License (Modified MIT) | https://github.com/g-truc/glm |
| stb | `external/stb`, selected headers in `external/include` | MIT or Public Domain | https://github.com/nothings/stb |
| nanoflann | `external/include/nanoflann.hpp` | BSD 2-Clause style license | https://github.com/jlblancoc/nanoflann |
| glad-generated OpenGL loader | `external/glad` | glad-generated source; includes Khronos MIT-style platform header | https://glad.dav1d.de/ |

Additional license files are kept next to the vendored sources, including:

- `external/imgui/LICENSE.txt`
- `external/implot/LICENSE`
- `external/ImGuiFileDialog/LICENSE`
- `external/glm/copying.txt`
- `external/stb/LICENSE`

The project can also be built against system or package-manager dependencies
such as GLFW, HDF5, Eigen, FFTW, ZeroMQ, nlohmann-json, Vulkan SDK/MoltenVK,
and platform SDKs. Those dependencies are not redistributed by this repository
unless explicitly present under `external/`.
