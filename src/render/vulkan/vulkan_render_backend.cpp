#include "render/render_backend.h"

#include "platform/vulkan_context.h"
#include "image/rgb_image.h"
#include "projection/projection_map_ui_state.h"
#include "render/colormap_defs.h"
#include "render/render_resources.h"
#include "render/render_system.h"

#include <vulkan/vulkan.h>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <utility>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace {

const char* VkResultName(VkResult result)
{
  switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_EVENT_SET: return "VK_EVENT_SET";
    case VK_EVENT_RESET: return "VK_EVENT_RESET";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    default: return "VK_RESULT_UNKNOWN";
  }
}

bool Check(VkResult result, const char* what)
{
  if (result == VK_SUCCESS) {
    return true;
  }
  std::cerr << what << " failed: " << VkResultName(result) << std::endl;
  return false;
}

std::vector<std::uint32_t> ReadSpirv(const std::string& path)
{
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    std::cerr << "Failed to open Vulkan shader: " << path << std::endl;
    return {};
  }

  const std::streamsize size = file.tellg();
  if (size <= 0 || (size % 4) != 0) {
    std::cerr << "Invalid Vulkan shader size: " << path << std::endl;
    return {};
  }

  std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char*>(words.data()), size);
  if (!file) {
    std::cerr << "Failed to read Vulkan shader: " << path << std::endl;
    return {};
  }
  return words;
}

std::string ShaderPath(const char* fileName)
{
#ifdef PARTICLE_VIS_VULKAN_SHADER_DIR
  return std::string(PARTICLE_VIS_VULKAN_SHADER_DIR) + "/" + fileName;
#else
  return std::string(fileName);
#endif
}

std::uint32_t FindMemoryType(VkPhysicalDevice physicalDevice,
                             std::uint32_t typeFilter,
                             VkMemoryPropertyFlags properties)
{
  VkPhysicalDeviceMemoryProperties memoryProperties{};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
  for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
    if ((typeFilter & (1u << i)) &&
        (memoryProperties.memoryTypes[i].propertyFlags & properties) ==
          properties) {
      return i;
    }
  }
  return UINT32_MAX;
}

struct VulkanBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
};

struct VulkanImage {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};

struct VisualUniform {
  glm::mat4 mvp{1.0f};
  glm::vec4 pointSizesA{1.0f};
  glm::vec4 pointSizesB{1.0f};
  glm::vec4 valueMinA{0.0f};
  glm::vec4 valueMinB{0.0f};
  glm::vec4 valueMaxA{1.0f};
  glm::vec4 valueMaxB{1.0f};
  glm::uvec4 colormapA{0u};
  glm::uvec4 colormapB{0u};
  glm::uvec4 masks{0u};
  glm::vec4 fixedColor{1.0f, 0.9f, 0.2f, 0.92f};
  glm::vec4 renderParams{0.92f, 11.0f, 0.0f, 0.0f};
};

struct SolidUniform {
  glm::mat4 view{1.0f};
  glm::mat4 projection{1.0f};
};

struct SolidPushConstants {
  float opacityScale = 1.0f;
};

struct SolidVertex {
  glm::vec3 pos{0.0f};
};

struct SolidInstance {
  glm::mat4 model{1.0f};
  glm::vec3 color{1.0f};
  float opacity = 1.0f;
};

struct SolidMesh {
  VulkanBuffer vertexBuffer;
  VulkanBuffer indexBuffer;
  std::uint32_t indexCount = 0;
};

struct SolidInstanceSet {
  VulkanBuffer buffer;
  std::size_t count = 0;
  RenderSceneVersion version = 0;
};

struct LineVertex {
  glm::vec3 pos{0.0f};
  glm::vec4 color{1.0f};
};

struct LineVertexSet {
  VulkanBuffer buffer;
  std::size_t count = 0;
  RenderSceneVersion version = 0;
};

constexpr std::uint32_t kColormapAtlasWidth = 256;

float Lerp(float a, float b, float t)
{
  return a + (b - a) * t;
}

std::array<unsigned char, 4> SampleColormap(const ColormapDef& def, float t)
{
  if (!def.data || def.count <= 0) {
    return {255, 255, 255, 255};
  }
  if (def.count == 1) {
    return {
      static_cast<unsigned char>(std::clamp(def.data[0], 0.0f, 1.0f) * 255.0f),
      static_cast<unsigned char>(std::clamp(def.data[1], 0.0f, 1.0f) * 255.0f),
      static_cast<unsigned char>(std::clamp(def.data[2], 0.0f, 1.0f) * 255.0f),
      255
    };
  }

  const float x = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(def.count - 1);
  const int i0 = std::clamp(static_cast<int>(x), 0, def.count - 1);
  const int i1 = std::min(i0 + 1, def.count - 1);
  const float f = x - static_cast<float>(i0);
  std::array<unsigned char, 4> rgba{};
  for (int c = 0; c < 3; ++c) {
    const float v0 = def.data[i0 * 3 + c];
    const float v1 = def.data[i1 * 3 + c];
    rgba[c] = static_cast<unsigned char>(
      std::clamp(Lerp(v0, v1, f), 0.0f, 1.0f) * 255.0f);
  }
  rgba[3] = 255;
  return rgba;
}

std::vector<unsigned char> BuildColormapAtlas()
{
  const ColormapDef* defs = AvailableColormaps();
  const int count = AvailableColormapCount();
  std::vector<unsigned char> atlas(static_cast<std::size_t>(kColormapAtlasWidth) *
                                   static_cast<std::size_t>(count) * 4u);
  for (int row = 0; row < count; ++row) {
    for (std::uint32_t x = 0; x < kColormapAtlasWidth; ++x) {
      const float t = static_cast<float>(x) /
                      static_cast<float>(kColormapAtlasWidth - 1);
      const std::array<unsigned char, 4> rgba = SampleColormap(defs[row], t);
      const std::size_t dst =
        (static_cast<std::size_t>(row) * kColormapAtlasWidth + x) * 4u;
      atlas[dst + 0] = rgba[0];
      atlas[dst + 1] = rgba[1];
      atlas[dst + 2] = rgba[2];
      atlas[dst + 3] = rgba[3];
    }
  }
  return atlas;
}

ImU32 SampleColormapColorU32(int colormapIndex, float t)
{
  const ColormapDef* defs = AvailableColormaps();
  const int count = AvailableColormapCount();
  if (!defs || count <= 0) {
    return IM_COL32(255, 255, 255, 255);
  }
  const int index = std::clamp(colormapIndex, 0, count - 1);
  const std::array<unsigned char, 4> rgba = SampleColormap(defs[index], t);
  return IM_COL32(rgba[0], rgba[1], rgba[2], 255);
}

struct ColorbarLayoutPixels {
  float left = 0.0f;
  float right = 0.0f;
  float top = 0.0f;
  float bottom = 0.0f;
  float offsetX = 0.0f;
  float offsetY = 0.0f;
};

ColorbarLayoutPixels ComputeColorbarLayoutPixels(
  const RenderFrameState& frame)
{
  const auto& settings = frame.runtime.colorbar.layout;
  const float width = static_cast<float>(frame.viewport.width);
  const float height = static_cast<float>(frame.viewport.height);

  ColorbarLayoutPixels layout;
  layout.left = width - settings.width - settings.margin;
  layout.right = width - settings.margin;
  layout.bottom = height - settings.margin;
  layout.top = height - settings.height - settings.margin;
  layout.offsetX = static_cast<float>(frame.viewport.x);
  layout.offsetY = static_cast<float>(frame.viewport.y);
  return layout;
}

ImVec2 PhysicalToImGui(float x, float y, const ColorbarLayoutPixels& layout)
{
  const ImGuiIO& io = ImGui::GetIO();
  const float scaleX = io.DisplayFramebufferScale.x > 0.0f
                         ? io.DisplayFramebufferScale.x
                         : 1.0f;
  const float scaleY = io.DisplayFramebufferScale.y > 0.0f
                         ? io.DisplayFramebufferScale.y
                         : 1.0f;
  return ImVec2((x + layout.offsetX) / scaleX,
                (y + layout.offsetY) / scaleY);
}

ImVec2 PhysicalToImGui(float x, float y)
{
  const ImGuiIO& io = ImGui::GetIO();
  const float scaleX = io.DisplayFramebufferScale.x > 0.0f
                         ? io.DisplayFramebufferScale.x
                         : 1.0f;
  const float scaleY = io.DisplayFramebufferScale.y > 0.0f
                         ? io.DisplayFramebufferScale.y
                         : 1.0f;
  return ImVec2(x / scaleX, y / scaleY);
}

glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback)
{
  const float len2 = glm::dot(v, v);
  if (len2 <= 1.0e-20f) {
    return fallback;
  }
  return v / std::sqrt(len2);
}

struct SolidMeshData {
  std::vector<SolidVertex> vertices;
  std::vector<std::uint32_t> indices;
};

SolidMeshData BuildCubeMeshData()
{
  SolidMeshData mesh;
  const SolidVertex vertices[] = {
    {{-0.5f, -0.5f, -0.5f}},
    {{ 0.5f, -0.5f, -0.5f}},
    {{ 0.5f,  0.5f, -0.5f}},
    {{-0.5f,  0.5f, -0.5f}},
    {{-0.5f, -0.5f,  0.5f}},
    {{ 0.5f, -0.5f,  0.5f}},
    {{ 0.5f,  0.5f,  0.5f}},
    {{-0.5f,  0.5f,  0.5f}},
  };
  const std::uint32_t indices[] = {
    0, 1, 2, 2, 3, 0,
    4, 5, 6, 6, 7, 4,
    4, 0, 3, 3, 7, 4,
    1, 5, 6, 6, 2, 1,
    4, 5, 1, 1, 0, 4,
    3, 2, 6, 6, 7, 3,
  };
  mesh.vertices.assign(std::begin(vertices), std::end(vertices));
  mesh.indices.assign(std::begin(indices), std::end(indices));
  return mesh;
}

SolidMeshData BuildSphereMeshData(int stacks = 32, int slices = 64)
{
  SolidMeshData mesh;
  for (int i = 0; i <= stacks; ++i) {
    const float v = static_cast<float>(i) / static_cast<float>(stacks);
    const float phi = glm::pi<float>() * (v - 0.5f);
    const float z = std::sin(phi);
    const float r = std::cos(phi);
    for (int j = 0; j <= slices; ++j) {
      const float u = static_cast<float>(j) / static_cast<float>(slices);
      const float theta = 2.0f * glm::pi<float>() * u;
      mesh.vertices.push_back({{r * std::cos(theta),
                                r * std::sin(theta),
                                z}});
    }
  }

  for (int i = 0; i < stacks; ++i) {
    for (int j = 0; j < slices; ++j) {
      const std::uint32_t a =
        static_cast<std::uint32_t>(i * (slices + 1) + j);
      const std::uint32_t b =
        static_cast<std::uint32_t>((i + 1) * (slices + 1) + j);
      mesh.indices.insert(mesh.indices.end(),
                          {a, b, b + 1u, a, b + 1u, a + 1u});
    }
  }
  return mesh;
}

SolidMeshData BuildDiskMeshData(int slices = 64)
{
  SolidMeshData mesh;
  mesh.vertices.push_back({{0.0f,  0.5f, 0.0f}});
  mesh.vertices.push_back({{0.0f, -0.5f, 0.0f}});

  for (int i = 0; i <= slices; ++i) {
    const float th = 2.0f * glm::pi<float>() *
                     static_cast<float>(i) / static_cast<float>(slices);
    const float x = std::cos(th);
    const float z = std::sin(th);
    mesh.vertices.push_back({{x,  0.5f, z}});
    mesh.vertices.push_back({{x, -0.5f, z}});
  }

  for (int i = 0; i < slices; ++i) {
    const std::uint32_t ii = static_cast<std::uint32_t>(i);
    mesh.indices.insert(mesh.indices.end(),
                        {0u, 2u + ii * 2u, 2u + (ii + 1u) * 2u});
    mesh.indices.insert(mesh.indices.end(),
                        {1u, 3u + (ii + 1u) * 2u, 3u + ii * 2u});
  }
  for (int i = 0; i < slices; ++i) {
    const std::uint32_t a = 2u + static_cast<std::uint32_t>(i) * 2u;
    const std::uint32_t b = a + 1u;
    const std::uint32_t c = 2u + static_cast<std::uint32_t>(i + 1) * 2u;
    const std::uint32_t d = c + 1u;
    mesh.indices.insert(mesh.indices.end(), {a, b, c, c, b, d});
  }
  return mesh;
}

const std::vector<std::pair<int, int>>& AllCuboidEdges()
{
  static const std::vector<std::pair<int, int>> edges = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7},
  };
  return edges;
}

const std::vector<std::pair<int, int>>& SelectedCuboidAxisEdges(
  CuboidAxis axis)
{
  static const std::vector<std::pair<int, int>> xEdges = {
    {0, 1}, {3, 2}, {4, 5}, {7, 6},
  };
  static const std::vector<std::pair<int, int>> yEdges = {
    {1, 2}, {0, 3}, {5, 6}, {4, 7},
  };
  static const std::vector<std::pair<int, int>> zEdges = {
    {0, 4}, {1, 5}, {2, 6}, {3, 7},
  };

  switch (axis) {
    case CuboidAxis::X: return xEdges;
    case CuboidAxis::Y: return yEdges;
    case CuboidAxis::Z: return zEdges;
  }
  return zEdges;
}

void AppendLinePair(std::vector<LineVertex>& out,
                    const glm::vec3& a,
                    const glm::vec3& b,
                    const glm::vec4& color)
{
  out.push_back({a, color});
  out.push_back({b, color});
}

void AppendCuboidEdges(std::vector<LineVertex>& out,
                       const CuboidObject& cuboid,
                       const std::vector<std::pair<int, int>>& edges,
                       const glm::vec4& color)
{
  const std::array<glm::vec3, 8> corners = computeCuboidCorners(cuboid);
  for (const auto& edge : edges) {
    AppendLinePair(out, corners[edge.first], corners[edge.second], color);
  }
}

} // namespace

class VulkanRenderBackend final : public RenderBackend {
public:
  explicit VulkanRenderBackend(VulkanContext& context)
    : context_(context)
  {
  }

  void init() override
  {
    device_ = context_.device();
    physicalDevice_ = context_.physicalDevice();
    createCommandPool();
    context_.setPreImGuiDrawCallback([this](VkCommandBuffer commandBuffer) {
      draw(commandBuffer);
    });
  }

  void destroy() override
  {
    context_.setPreImGuiDrawCallback({});
    if (device_ != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(device_);
    }
    destroyPipeline();
    destroySolidResources();
    destroyProjectionPreviewResources();
    destroyBuffer(particleBuffer_);
    destroyBuffer(stressParticleBuffer_);
    for (VulkanBuffer& buffer : visualBuffers_) {
      destroyBuffer(buffer);
    }
    destroyImage(colormapAtlas_);
    if (colormapSampler_ != VK_NULL_HANDLE) {
      vkDestroySampler(device_, colormapSampler_, nullptr);
      colormapSampler_ = VK_NULL_HANDLE;
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
      descriptorPool_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
      descriptorSetLayout_ = VK_NULL_HANDLE;
    }
    if (commandPool_ != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device_, commandPool_, nullptr);
      commandPool_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
  }

  void render(const RenderFrameState& frame,
              const RenderSceneData& scene) override
  {
    if (device_ == VK_NULL_HANDLE || !frame.valid) {
      return;
    }
    frame_ = frame;
    visualDirty_ = true;
    hiddenTypeMask_ = 0;
    logTypeMask_ = 0;
    periodicTypeMask_ = 0;
    for (std::size_t i = 0; i < pointSizes_.size(); ++i) {
      const auto& cfg = frame.particleVisual.types[i];
      pointSizes_[i] = cfg.pointSize;
      valueMin_[i] = cfg.colorMin;
      valueMax_[i] = cfg.colorMax;
      const int maxColormap = std::max(AvailableColormapCount() - 1, 0);
      colormap_[i] =
        static_cast<std::uint32_t>(std::clamp(cfg.colormapIndex,
                                             0,
                                             maxColormap));
      if (cfg.hideParticles) {
        hiddenTypeMask_ |= (1u << i);
      }
      if (cfg.useLogScale) {
        logTypeMask_ |= (1u << i);
      }
      if (cfg.periodicColorBar) {
        periodicTypeMask_ |= (1u << i);
      }
    }
    syncParticles(scene);
    syncStressParticles(scene);
    syncSolidInstances(scene);
    syncLineVertices(scene);
    syncVisualUniform(0, false);
    syncVisualUniform(1, true);
    syncSolidUniform();
    drawColorbarOverlay();
    drawGizmoOverlays();
  }

  void updateProjectionPreview(const RgbImage& image) override
  {
    uploadProjectionPreview(image);
  }

  ProjectionPreviewUIState makeProjectionPreviewUIState() const override
  {
    return preview_;
  }

  RenderBackendCapabilities capabilities() const override
  {
    RenderBackendCapabilities caps;
    caps.particles = true;
    caps.instancedObjects = true;
    caps.lines = true;
    caps.polyhedra = true;
    caps.colorbar = true;
    caps.gizmos = true;
    caps.projectionPreview = true;
    return caps;
  }

private:
  bool projectWorldToImGui(const glm::vec3& pos, ImVec2& out) const
  {
    if (!frame_.valid || frame_.viewport.width <= 0 || frame_.viewport.height <= 0) {
      return false;
    }

    const glm::mat4 mvp =
      frame_.matrices.projection * frame_.matrices.view * frame_.matrices.model;
    const glm::vec4 clip = mvp * glm::vec4(pos, 1.0f);
    if (clip.w <= 1.0e-8f) {
      return false;
    }

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) {
      return false;
    }

    const float x =
      static_cast<float>(frame_.viewport.x) +
      (ndc.x * 0.5f + 0.5f) * static_cast<float>(frame_.viewport.width);
    const float y =
      static_cast<float>(frame_.viewport.y) +
      (1.0f - (ndc.y * 0.5f + 0.5f)) *
        static_cast<float>(frame_.viewport.height);
    out = PhysicalToImGui(x, y);
    return true;
  }

  void drawProjectedWorldLine(ImDrawList& drawList,
                              const glm::vec3& a,
                              const glm::vec3& b,
                              ImU32 color,
                              float thickness) const
  {
    ImVec2 p0;
    ImVec2 p1;
    if (projectWorldToImGui(a, p0) && projectWorldToImGui(b, p1)) {
      drawList.AddLine(p0, p1, color, thickness);
    }
  }

  void drawCrossGizmoOverlay(ImDrawList& drawList) const
  {
    const auto& cross = frame_.runtime.crossGizmo;
    if (!cross.show) {
      return;
    }

    const glm::vec3 forward =
      SafeNormalize(frame_.camera.cameraTarget - frame_.camera.cameraPos,
                    glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::vec3 right =
      SafeNormalize(glm::cross(forward, frame_.camera.cameraUp),
                    glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 up =
      SafeNormalize(glm::cross(right, forward),
                    glm::vec3(0.0f, 1.0f, 0.0f));

    const glm::vec3 center = frame_.camera.cameraTarget;
    const float size = std::max(cross.size, 0.0f);
    constexpr float kCrossThickness = 2.0f;
    const ImU32 white = IM_COL32(255, 255, 255, 235);
    drawProjectedWorldLine(drawList,
                           center - (right + up) * size,
                           center + (right + up) * size,
                           white,
                           kCrossThickness);
    drawProjectedWorldLine(drawList,
                           center - (right - up) * size,
                           center + (right - up) * size,
                           white,
                           kCrossThickness);
    drawProjectedWorldLine(drawList,
                           center - up * size,
                           center + up * size,
                           white,
                           kCrossThickness);
  }

  void drawCoordAxesOverlay(ImDrawList& drawList) const
  {
    if (!frame_.runtime.coordAxes.show || frame_.viewport.width <= 0 ||
        frame_.viewport.height <= 0) {
      return;
    }

    const float axisLength =
      0.085f * static_cast<float>(std::min(frame_.viewport.width,
                                           frame_.viewport.height));
    const float originX =
      static_cast<float>(frame_.viewport.x) +
      static_cast<float>(frame_.viewport.width) * 0.90f;
    const float originY =
      static_cast<float>(frame_.viewport.y) +
      static_cast<float>(frame_.viewport.height) * 0.84f;
    const ImVec2 origin = PhysicalToImGui(originX, originY);
    const glm::mat3 viewRot(frame_.matrices.view);

    auto drawAxis = [&](const glm::vec3& axis,
                        ImU32 color,
                        const char* label) {
      const glm::vec3 rotated = viewRot * axis;
      const ImVec2 end = PhysicalToImGui(originX + rotated.x * axisLength,
                                         originY - rotated.y * axisLength);
      drawList.AddLine(origin, end, color, 3.0f);
      drawList.AddText(ImVec2(end.x + 4.0f, end.y + 4.0f),
                       color,
                       label);
    };

    drawAxis(glm::vec3(1.0f, 0.0f, 0.0f), IM_COL32(255, 80, 80, 240), "X");
    drawAxis(glm::vec3(0.0f, 1.0f, 0.0f), IM_COL32(80, 255, 80, 240), "Y");
    drawAxis(glm::vec3(0.0f, 0.0f, 1.0f), IM_COL32(240, 240, 255, 240), "Z");
  }

  void drawGizmoOverlays() const
  {
    if (!frame_.valid) {
      return;
    }
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList) {
      return;
    }
    drawCrossGizmoOverlay(*drawList);
    drawCoordAxesOverlay(*drawList);
  }

  void drawColorbarOverlay() const
  {
    if (!frame_.valid || !frame_.runtime.colorbar.show) {
      return;
    }
    const int particleType = std::clamp(frame_.runtime.colorbar.sourceParticleType,
                                       0,
                                       kNumParticleTypes - 1);
    const auto& visual = frame_.particleVisual.types[particleType];
    const ColorbarLayoutPixels layout = ComputeColorbarLayoutPixels(frame_);
    if (layout.right <= layout.left || layout.bottom <= layout.top) {
      return;
    }

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList) {
      return;
    }

    constexpr int kSegments = 96;
    for (int i = 0; i < kSegments; ++i) {
      const float t0 = static_cast<float>(i) / static_cast<float>(kSegments);
      const float t1 = static_cast<float>(i + 1) / static_cast<float>(kSegments);
      const float x0 = layout.left + t0 * (layout.right - layout.left);
      const float x1 = layout.left + t1 * (layout.right - layout.left);
      const ImVec2 p0 = PhysicalToImGui(x0, layout.top, layout);
      const ImVec2 p1 = PhysicalToImGui(x1, layout.bottom, layout);
      const ImU32 c0 = SampleColormapColorU32(visual.colormapIndex, t0);
      const ImU32 c1 = SampleColormapColorU32(visual.colormapIndex, t1);
      drawList->AddRectFilledMultiColor(p0,
                                        p1,
                                        c0,
                                        c1,
                                        c1,
                                        c0);
    }

    const ImVec2 borderMin = PhysicalToImGui(layout.left, layout.top, layout);
    const ImVec2 borderMax = PhysicalToImGui(layout.right, layout.bottom, layout);
    drawList->AddRect(borderMin, borderMax, IM_COL32(255, 255, 255, 220));

    const int numTicks = std::max(frame_.runtime.colorbar.numTicks, 2);
    for (int i = 0; i < numTicks; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(numTicks - 1);
      const float x = layout.left + t * (layout.right - layout.left);
      const float value = visual.colorMin + t * (visual.colorMax - visual.colorMin);
      const ImVec2 tick0 = PhysicalToImGui(x, layout.bottom, layout);
      const ImVec2 tick1 = PhysicalToImGui(x, layout.bottom + 4.0f, layout);
      drawList->AddLine(tick0, tick1, IM_COL32(255, 255, 255, 220));

      char label[32];
      std::snprintf(label, sizeof(label), "%.2f", value);
      ImVec2 textPos = PhysicalToImGui(x, layout.bottom + 7.0f, layout);
      textPos.x = std::floor(textPos.x + 0.5f);
      textPos.y = std::floor(textPos.y + 0.5f);
      drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), label);
    }
  }

  void syncParticles(const RenderSceneData& scene)
  {
    if (scene.particlesVersion == particleVersion_) {
      particleCount_ = scene.particles.size();
      return;
    }

    particleVersion_ = scene.particlesVersion;
    particleCount_ = scene.particles.size();
    const VkDeviceSize bytes =
      static_cast<VkDeviceSize>(scene.particles.size() *
                                sizeof(RenderParticle));
    if (bytes == 0) {
      return;
    }
    if (particleBuffer_.size < bytes) {
      destroyBuffer(particleBuffer_);
      particleBuffer_ = createBuffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
    if (particleBuffer_.memory == VK_NULL_HANDLE) {
      return;
    }

    void* mapped = nullptr;
    if (!Check(vkMapMemory(device_,
                           particleBuffer_.memory,
                           0,
                           bytes,
                           0,
                           &mapped),
               "vkMapMemory")) {
      return;
    }
    std::memcpy(mapped, scene.particles.data(), static_cast<std::size_t>(bytes));
    vkUnmapMemory(device_, particleBuffer_.memory);
  }

  void syncStressParticles(const RenderSceneData& scene)
  {
    if (scene.stressParticlesVersion == stressParticleVersion_) {
      stressParticleCount_ = scene.stressParticles.size();
      return;
    }

    stressParticleVersion_ = scene.stressParticlesVersion;
    stressParticleCount_ = scene.stressParticles.size();
    const VkDeviceSize bytes =
      static_cast<VkDeviceSize>(scene.stressParticles.size() *
                                sizeof(RenderParticle));
    if (bytes == 0) {
      return;
    }
    if (stressParticleBuffer_.size < bytes) {
      destroyBuffer(stressParticleBuffer_);
      stressParticleBuffer_ = createBuffer(bytes,
                                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
    if (stressParticleBuffer_.memory == VK_NULL_HANDLE) {
      return;
    }

    void* mapped = nullptr;
    if (!Check(vkMapMemory(device_,
                           stressParticleBuffer_.memory,
                           0,
                           bytes,
                           0,
                           &mapped),
               "vkMapMemory(stress particles)")) {
      return;
    }
    std::memcpy(mapped,
                scene.stressParticles.data(),
                static_cast<std::size_t>(bytes));
    vkUnmapMemory(device_, stressParticleBuffer_.memory);
  }

  bool uploadToBuffer(VulkanBuffer& buffer,
                      const void* data,
                      VkDeviceSize bytes,
                      VkBufferUsageFlags usage,
                      const char* label)
  {
    if (bytes == 0) {
      return true;
    }
    if (buffer.size < bytes) {
      destroyBuffer(buffer);
      buffer = createBuffer(bytes, usage);
    }
    if (buffer.memory == VK_NULL_HANDLE) {
      return false;
    }

    void* mapped = nullptr;
    if (!Check(vkMapMemory(device_, buffer.memory, 0, bytes, 0, &mapped),
               label)) {
      return false;
    }
    std::memcpy(mapped, data, static_cast<std::size_t>(bytes));
    vkUnmapMemory(device_, buffer.memory);
    return true;
  }

  bool uploadSolidMesh(SolidMesh& mesh, const SolidMeshData& data)
  {
    const VkDeviceSize vertexBytes =
      static_cast<VkDeviceSize>(data.vertices.size() * sizeof(SolidVertex));
    const VkDeviceSize indexBytes =
      static_cast<VkDeviceSize>(data.indices.size() * sizeof(std::uint32_t));
    if (vertexBytes == 0 || indexBytes == 0) {
      return false;
    }

    const bool vertexOk =
      uploadToBuffer(mesh.vertexBuffer,
                     data.vertices.data(),
                     vertexBytes,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     "vkMapMemory(solid vertices)");
    const bool indexOk =
      uploadToBuffer(mesh.indexBuffer,
                     data.indices.data(),
                     indexBytes,
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     "vkMapMemory(solid indices)");
    if (vertexOk && indexOk) {
      mesh.indexCount = static_cast<std::uint32_t>(data.indices.size());
      return true;
    }
    return false;
  }

  bool ensureSolidMeshes()
  {
    if (cubeMesh_.indexCount == 0 && !uploadSolidMesh(cubeMesh_,
                                                      BuildCubeMeshData())) {
      return false;
    }
    if (ellipsoidMesh_.indexCount == 0 &&
        !uploadSolidMesh(ellipsoidMesh_, BuildSphereMeshData())) {
      return false;
    }
    if (diskMesh_.indexCount == 0 &&
        !uploadSolidMesh(diskMesh_, BuildDiskMeshData())) {
      return false;
    }
    return true;
  }

  bool ensureSolidDescriptorResources()
  {
    if (solidDescriptorSetLayout_ != VK_NULL_HANDLE) {
      return solidDescriptorSet_ != VK_NULL_HANDLE &&
             solidUniformBuffer_.buffer != VK_NULL_HANDLE;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (!Check(vkCreateDescriptorSetLayout(device_,
                                           &layoutInfo,
                                           nullptr,
                                           &solidDescriptorSetLayout_),
               "vkCreateDescriptorSetLayout(solid)")) {
      return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (!Check(vkCreateDescriptorPool(device_,
                                      &poolInfo,
                                      nullptr,
                                      &solidDescriptorPool_),
               "vkCreateDescriptorPool(solid)")) {
      return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = solidDescriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &solidDescriptorSetLayout_;
    if (!Check(vkAllocateDescriptorSets(device_,
                                        &allocInfo,
                                        &solidDescriptorSet_),
               "vkAllocateDescriptorSets(solid)")) {
      return false;
    }

    solidUniformBuffer_ = createBuffer(sizeof(SolidUniform),
                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    if (solidUniformBuffer_.buffer == VK_NULL_HANDLE) {
      return false;
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = solidUniformBuffer_.buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(SolidUniform);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = solidDescriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return true;
  }

  void syncSolidUniform()
  {
    if (!frame_.valid || !ensureSolidDescriptorResources()) {
      return;
    }
    SolidUniform uniform;
    uniform.view = frame_.matrices.view;
    uniform.projection = frame_.matrices.projection;
    uniform.projection[1][1] *= -1.0f;
    uploadToBuffer(solidUniformBuffer_,
                   &uniform,
                   sizeof(uniform),
                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                   "vkMapMemory(solid uniform)");
  }

  void syncSolidInstanceSet(const std::vector<InstancedSolidItem>& items,
                            RenderSceneVersion version,
                            SolidInstanceSet& set,
                            const char* label)
  {
    if (set.version == version) {
      set.count = items.size();
      return;
    }

    set.version = version;
    set.count = items.size();
    if (items.empty()) {
      return;
    }

    std::vector<SolidInstance> instances;
    instances.reserve(items.size());
    for (const InstancedSolidItem& item : items) {
      SolidInstance instance;
      instance.model = item.model;
      instance.color = item.color;
      instance.opacity = item.opacity;
      instances.push_back(instance);
    }

    uploadToBuffer(set.buffer,
                   instances.data(),
                   static_cast<VkDeviceSize>(instances.size() *
                                             sizeof(SolidInstance)),
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                   label);
  }

  void syncSolidInstances(const RenderSceneData& scene)
  {
    if (!ensureSolidMeshes()) {
      return;
    }
    syncSolidInstanceSet(scene.cubes,
                         scene.cubesVersion,
                         cubeInstances_,
                         "vkMapMemory(cube instances)");
    syncSolidInstanceSet(scene.ellipsoids,
                         scene.ellipsoidsVersion,
                         ellipsoidInstances_,
                         "vkMapMemory(ellipsoid instances)");
    syncSolidInstanceSet(scene.disks,
                         scene.disksVersion,
                         diskInstances_,
                         "vkMapMemory(disk instances)");
  }

  void syncLineVertexSet(const std::vector<LineVertex>& vertices,
                         RenderSceneVersion version,
                         LineVertexSet& set,
                         const char* label)
  {
    if (set.version == version) {
      set.count = vertices.size();
      return;
    }

    set.version = version;
    set.count = vertices.size();
    if (vertices.empty()) {
      return;
    }

    uploadToBuffer(set.buffer,
                   vertices.data(),
                   static_cast<VkDeviceSize>(vertices.size() *
                                             sizeof(LineVertex)),
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                   label);
  }

  static std::vector<LineVertex> BuildLineVertices(
    const std::vector<LineRenderItem>& lines)
  {
    std::vector<LineVertex> vertices;
    for (const LineRenderItem& item : lines) {
      const glm::vec4 color(item.color, item.opacity);
      if (item.mode == LinePrimitiveMode::List) {
        for (std::size_t i = 1; i < item.points.size(); i += 2) {
          AppendLinePair(vertices, item.points[i - 1], item.points[i], color);
        }
      } else {
        for (std::size_t i = 1; i < item.points.size(); ++i) {
          AppendLinePair(vertices, item.points[i - 1], item.points[i], color);
        }
      }
    }
    return vertices;
  }

  static std::vector<LineVertex> BuildCuboidVertices(
    const std::vector<CuboidRenderItem>& cuboids)
  {
    std::vector<LineVertex> vertices;
    for (const CuboidRenderItem& item : cuboids) {
      AppendCuboidEdges(vertices,
                        item.cuboid,
                        AllCuboidEdges(),
                        item.cuboid.edgeColor);
      if (item.showHighlight) {
        AppendCuboidEdges(vertices,
                          item.cuboid,
                          SelectedCuboidAxisEdges(item.selectedAxis),
                          item.highlightColor);
      }
    }
    return vertices;
  }

  static std::vector<LineVertex> BuildPolyhedronVertices(
    const std::vector<PolyhedronRenderItem>& polyhedra)
  {
    std::vector<LineVertex> vertices;
    for (const PolyhedronRenderItem& item : polyhedra) {
      const glm::vec4 color(item.color, item.opacity);
      for (std::size_t i = 1; i < item.vertices.size(); i += 2) {
        AppendLinePair(vertices, item.vertices[i - 1], item.vertices[i], color);
      }
    }
    return vertices;
  }

  void syncLineVertices(const RenderSceneData& scene)
  {
    syncLineVertexSet(BuildLineVertices(scene.lines),
                      scene.linesVersion,
                      lineVertices_,
                      "vkMapMemory(line vertices)");
    syncLineVertexSet(BuildCuboidVertices(scene.cuboids),
                      scene.cuboidsVersion,
                      cuboidVertices_,
                      "vkMapMemory(cuboid vertices)");
    syncLineVertexSet(BuildPolyhedronVertices(scene.polyhedra),
                      scene.polyhedraVersion,
                      polyhedronVertices_,
                      "vkMapMemory(polyhedron vertices)");
  }

  VulkanBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage)
  {
    VulkanBuffer out;
    out.size = size;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!Check(vkCreateBuffer(device_, &bufferInfo, nullptr, &out.buffer),
               "vkCreateBuffer")) {
      return out;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, out.buffer, &requirements);
    const std::uint32_t memoryType =
      FindMemoryType(physicalDevice_,
                     requirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memoryType == UINT32_MAX) {
      std::cerr << "No host-visible Vulkan memory type for particle buffer."
                << std::endl;
      vkDestroyBuffer(device_, out.buffer, nullptr);
      out.buffer = VK_NULL_HANDLE;
      return out;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = memoryType;
    if (!Check(vkAllocateMemory(device_, &allocInfo, nullptr, &out.memory),
               "vkAllocateMemory")) {
      vkDestroyBuffer(device_, out.buffer, nullptr);
      out.buffer = VK_NULL_HANDLE;
      return out;
    }
    Check(vkBindBufferMemory(device_, out.buffer, out.memory, 0),
          "vkBindBufferMemory");
    return out;
  }

  bool createCommandPool()
  {
    if (commandPool_ != VK_NULL_HANDLE) {
      return true;
    }
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = context_.queueFamily();
    return Check(vkCreateCommandPool(device_,
                                     &poolInfo,
                                     nullptr,
                                     &commandPool_),
                 "vkCreateCommandPool(render)");
  }

  VkCommandBuffer beginImmediateCommands()
  {
    if (!createCommandPool()) {
      return VK_NULL_HANDLE;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (!Check(vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer),
               "vkAllocateCommandBuffers(render)")) {
      return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!Check(vkBeginCommandBuffer(commandBuffer, &beginInfo),
               "vkBeginCommandBuffer(render upload)")) {
      vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
      return VK_NULL_HANDLE;
    }
    return commandBuffer;
  }

  bool endImmediateCommands(VkCommandBuffer commandBuffer)
  {
    if (commandBuffer == VK_NULL_HANDLE) {
      return false;
    }
    bool ok = Check(vkEndCommandBuffer(commandBuffer),
                    "vkEndCommandBuffer(render upload)");
    if (ok) {
      VkSubmitInfo submitInfo{};
      submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &commandBuffer;
      ok = Check(vkQueueSubmit(context_.queue(), 1, &submitInfo, VK_NULL_HANDLE),
                 "vkQueueSubmit(render upload)");
      if (ok) {
        ok = Check(vkQueueWaitIdle(context_.queue()),
                   "vkQueueWaitIdle(render upload)");
      }
    }
    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
    return ok;
  }

  bool transitionImageLayout(VkImage image,
                             VkImageLayout oldLayout,
                             VkImageLayout newLayout)
  {
    VkCommandBuffer commandBuffer = beginImmediateCommands();
    if (commandBuffer == VK_NULL_HANDLE) {
      return false;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(commandBuffer,
                         srcStage,
                         dstStage,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);
    return endImmediateCommands(commandBuffer);
  }

  bool copyBufferToImage(VkBuffer buffer,
                         VkImage image,
                         std::uint32_t width,
                         std::uint32_t height)
  {
    VkCommandBuffer commandBuffer = beginImmediateCommands();
    if (commandBuffer == VK_NULL_HANDLE) {
      return false;
    }

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(commandBuffer,
                           buffer,
                           image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &region);
    return endImmediateCommands(commandBuffer);
  }

  VulkanImage createImage(std::uint32_t width,
                          std::uint32_t height,
                          VkFormat format,
                          VkImageUsageFlags usage)
  {
    VulkanImage out;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!Check(vkCreateImage(device_, &imageInfo, nullptr, &out.image),
               "vkCreateImage(colormap)")) {
      return out;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, out.image, &requirements);
    const std::uint32_t memoryType =
      FindMemoryType(physicalDevice_,
                     requirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == UINT32_MAX) {
      std::cerr << "No device-local Vulkan memory type for colormap atlas."
                << std::endl;
      destroyImage(out);
      return out;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = memoryType;
    if (!Check(vkAllocateMemory(device_, &allocInfo, nullptr, &out.memory),
               "vkAllocateMemory(colormap)")) {
      destroyImage(out);
      return out;
    }
    Check(vkBindImageMemory(device_, out.image, out.memory, 0),
          "vkBindImageMemory(colormap)");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = out.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (!Check(vkCreateImageView(device_, &viewInfo, nullptr, &out.view),
               "vkCreateImageView(colormap)")) {
      destroyImage(out);
      return out;
    }
    return out;
  }

  bool ensureColormapAtlas()
  {
    if (colormapAtlas_.view != VK_NULL_HANDLE &&
        colormapSampler_ != VK_NULL_HANDLE) {
      return true;
    }

    const int colormapCount = AvailableColormapCount();
    if (colormapCount <= 0) {
      return false;
    }
    const std::vector<unsigned char> atlas = BuildColormapAtlas();
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(atlas.size());
    VulkanBuffer staging = createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (staging.memory == VK_NULL_HANDLE) {
      return false;
    }

    void* mapped = nullptr;
    if (!Check(vkMapMemory(device_, staging.memory, 0, bytes, 0, &mapped),
               "vkMapMemory(colormap staging)")) {
      destroyBuffer(staging);
      return false;
    }
    std::memcpy(mapped, atlas.data(), atlas.size());
    vkUnmapMemory(device_, staging.memory);

    colormapAtlas_ = createImage(kColormapAtlasWidth,
                                 static_cast<std::uint32_t>(colormapCount),
                                 VK_FORMAT_R8G8B8A8_UNORM,
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                   VK_IMAGE_USAGE_SAMPLED_BIT);
    if (colormapAtlas_.image == VK_NULL_HANDLE) {
      destroyBuffer(staging);
      return false;
    }

    const bool uploaded =
      transitionImageLayout(colormapAtlas_.image,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) &&
      copyBufferToImage(staging.buffer,
                        colormapAtlas_.image,
                        kColormapAtlasWidth,
                        static_cast<std::uint32_t>(colormapCount)) &&
      transitionImageLayout(colormapAtlas_.image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    destroyBuffer(staging);
    if (!uploaded) {
      destroyImage(colormapAtlas_);
      return false;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    if (!Check(vkCreateSampler(device_, &samplerInfo, nullptr, &colormapSampler_),
               "vkCreateSampler(colormap)")) {
      destroyImage(colormapAtlas_);
      return false;
    }
    return true;
  }

  bool ensureProjectionPreviewSampler()
  {
    if (previewSampler_ != VK_NULL_HANDLE) {
      return true;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    return Check(vkCreateSampler(device_, &samplerInfo, nullptr, &previewSampler_),
                 "vkCreateSampler(projection preview)");
  }

  void destroyProjectionPreviewResources()
  {
    if (previewDescriptorSet_ != VK_NULL_HANDLE) {
      ImGui_ImplVulkan_RemoveTexture(previewDescriptorSet_);
      previewDescriptorSet_ = VK_NULL_HANDLE;
    }
    destroyImage(previewImage_);
    if (previewSampler_ != VK_NULL_HANDLE) {
      vkDestroySampler(device_, previewSampler_, nullptr);
      previewSampler_ = VK_NULL_HANDLE;
    }
    preview_ = ProjectionPreviewUIState{};
    previewVersion_ = 0;
  }

  void uploadProjectionPreview(const RgbImage& image)
  {
    if (device_ == VK_NULL_HANDLE || image.width <= 0 || image.height <= 0 ||
        image.rgb.size() !=
          static_cast<std::size_t>(image.width * image.height * 3)) {
      return;
    }
    if (previewVersion_ == image.version && preview_.valid) {
      return;
    }
    if (!ensureProjectionPreviewSampler()) {
      return;
    }

    std::vector<unsigned char> rgba(
      static_cast<std::size_t>(image.width) *
      static_cast<std::size_t>(image.height) * 4u);
    for (std::size_t src = 0, dst = 0; src + 2 < image.rgb.size();
         src += 3, dst += 4) {
      rgba[dst + 0] = image.rgb[src + 0];
      rgba[dst + 1] = image.rgb[src + 1];
      rgba[dst + 2] = image.rgb[src + 2];
      rgba[dst + 3] = 255;
    }

    const bool sizeChanged =
      image.width != preview_.width || image.height != preview_.height ||
      previewImage_.image == VK_NULL_HANDLE;
    if (sizeChanged) {
      if (previewDescriptorSet_ != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(previewDescriptorSet_);
        previewDescriptorSet_ = VK_NULL_HANDLE;
      }
      destroyImage(previewImage_);
      previewImage_ = createImage(static_cast<std::uint32_t>(image.width),
                                  static_cast<std::uint32_t>(image.height),
                                  VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                    VK_IMAGE_USAGE_SAMPLED_BIT);
      if (previewImage_.image == VK_NULL_HANDLE) {
        preview_ = ProjectionPreviewUIState{};
        return;
      }
    }

    VulkanBuffer staging =
      createBuffer(static_cast<VkDeviceSize>(rgba.size()),
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (staging.memory == VK_NULL_HANDLE) {
      return;
    }

    void* mapped = nullptr;
    if (!Check(vkMapMemory(device_,
                           staging.memory,
                           0,
                           static_cast<VkDeviceSize>(rgba.size()),
                           0,
                           &mapped),
               "vkMapMemory(projection preview staging)")) {
      destroyBuffer(staging);
      return;
    }
    std::memcpy(mapped, rgba.data(), rgba.size());
    vkUnmapMemory(device_, staging.memory);

    const VkImageLayout oldLayout = sizeChanged
      ? VK_IMAGE_LAYOUT_UNDEFINED
      : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const bool uploaded =
      transitionImageLayout(previewImage_.image,
                            oldLayout,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) &&
      copyBufferToImage(staging.buffer,
                        previewImage_.image,
                        static_cast<std::uint32_t>(image.width),
                        static_cast<std::uint32_t>(image.height)) &&
      transitionImageLayout(previewImage_.image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    destroyBuffer(staging);
    if (!uploaded) {
      preview_ = ProjectionPreviewUIState{};
      return;
    }

    if (previewDescriptorSet_ == VK_NULL_HANDLE) {
      previewDescriptorSet_ =
        ImGui_ImplVulkan_AddTexture(previewSampler_,
                                    previewImage_.view,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    preview_.textureId = reinterpret_cast<void*>(previewDescriptorSet_);
    preview_.width = image.width;
    preview_.height = image.height;
    preview_.version = image.version;
    preview_.valid = previewDescriptorSet_ != VK_NULL_HANDLE;
    previewVersion_ = image.version;
  }

  bool ensureDescriptorResources()
  {
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
      return descriptorSets_[0] != VK_NULL_HANDLE &&
             descriptorSets_[1] != VK_NULL_HANDLE &&
             visualBuffers_[0].buffer != VK_NULL_HANDLE &&
             visualBuffers_[1].buffer != VK_NULL_HANDLE &&
             colormapAtlas_.view != VK_NULL_HANDLE &&
             colormapSampler_ != VK_NULL_HANDLE;
    }
    if (!ensureColormapAtlas()) {
      return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (!Check(vkCreateDescriptorSetLayout(device_,
                                           &layoutInfo,
                                           nullptr,
                                           &descriptorSetLayout_),
               "vkCreateDescriptorSetLayout")) {
      return false;
    }

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 2;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (!Check(vkCreateDescriptorPool(device_,
                                      &poolInfo,
                                      nullptr,
                                      &descriptorPool_),
               "vkCreateDescriptorPool(render)")) {
      return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount =
      static_cast<std::uint32_t>(descriptorSets_.size());
    std::array<VkDescriptorSetLayout, 2> setLayouts = {
      descriptorSetLayout_,
      descriptorSetLayout_
    };
    allocInfo.pSetLayouts = setLayouts.data();
    if (!Check(vkAllocateDescriptorSets(device_,
                                        &allocInfo,
                                        descriptorSets_.data()),
               "vkAllocateDescriptorSets")) {
      return false;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = colormapSampler_;
    imageInfo.imageView = colormapAtlas_.view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    for (std::size_t i = 0; i < visualBuffers_.size(); ++i) {
      visualBuffers_[i] = createBuffer(sizeof(VisualUniform),
                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
      if (visualBuffers_[i].buffer == VK_NULL_HANDLE) {
        return false;
      }

      VkDescriptorBufferInfo bufferInfo{};
      bufferInfo.buffer = visualBuffers_[i].buffer;
      bufferInfo.offset = 0;
      bufferInfo.range = sizeof(VisualUniform);

      std::array<VkWriteDescriptorSet, 2> writes{};
      writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[0].dstSet = descriptorSets_[i];
      writes[0].dstBinding = 0;
      writes[0].descriptorCount = 1;
      writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      writes[0].pBufferInfo = &bufferInfo;
      writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[1].dstSet = descriptorSets_[i];
      writes[1].dstBinding = 1;
      writes[1].descriptorCount = 1;
      writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writes[1].pImageInfo = &imageInfo;
      vkUpdateDescriptorSets(device_,
                             static_cast<std::uint32_t>(writes.size()),
                             writes.data(),
                             0,
                             nullptr);
    }

    visualDirty_ = true;
    return true;
  }

  void syncVisualUniform(std::size_t passIndex, bool fixedColor)
  {
    if (!ensureDescriptorResources() ||
        passIndex >= visualBuffers_.size() ||
        visualBuffers_[passIndex].memory == VK_NULL_HANDLE || !frame_.valid) {
      return;
    }

    glm::mat4 projection = frame_.matrices.projection;
    projection[1][1] *= -1.0f;

    VisualUniform uniform;
    uniform.mvp = projection * frame_.matrices.view * frame_.matrices.model;
    uniform.masks = glm::uvec4(hiddenTypeMask_,
                               logTypeMask_,
                               periodicTypeMask_,
                               0u);
    uniform.fixedColor = glm::vec4(1.0f, 0.9f, 0.2f, 0.92f);
    uniform.renderParams =
      glm::vec4(fixedColor ? 0.92f : 1.0f,
                static_cast<float>(AvailableColormapCount()),
                fixedColor ? 1.0f : 0.0f,
                fixedColor ? 1.35f : 1.0f);
    for (std::size_t i = 0; i < 4; ++i) {
      uniform.pointSizesA[i] = pointSizes_[i];
      uniform.valueMinA[i] = valueMin_[i];
      uniform.valueMaxA[i] = valueMax_[i];
      uniform.colormapA[i] = colormap_[i];
    }
    for (std::size_t i = 0; i < 2; ++i) {
      uniform.pointSizesB[i] = pointSizes_[i + 4];
      uniform.valueMinB[i] = valueMin_[i + 4];
      uniform.valueMaxB[i] = valueMax_[i + 4];
      uniform.colormapB[i] = colormap_[i + 4];
    }

    void* mapped = nullptr;
    if (!Check(vkMapMemory(device_,
                           visualBuffers_[passIndex].memory,
                           0,
                           sizeof(uniform),
                           0,
                           &mapped),
               "vkMapMemory(visual)")) {
      return;
    }
    std::memcpy(mapped, &uniform, sizeof(uniform));
    vkUnmapMemory(device_, visualBuffers_[passIndex].memory);
    visualDirty_ = false;
  }

  void destroyBuffer(VulkanBuffer& buffer)
  {
    if (device_ == VK_NULL_HANDLE) {
      return;
    }
    if (buffer.buffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, buffer.buffer, nullptr);
    }
    if (buffer.memory != VK_NULL_HANDLE) {
      vkFreeMemory(device_, buffer.memory, nullptr);
    }
    buffer = VulkanBuffer{};
  }

  void destroyImage(VulkanImage& image)
  {
    if (device_ == VK_NULL_HANDLE) {
      return;
    }
    if (image.view != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, image.view, nullptr);
    }
    if (image.image != VK_NULL_HANDLE) {
      vkDestroyImage(device_, image.image, nullptr);
    }
    if (image.memory != VK_NULL_HANDLE) {
      vkFreeMemory(device_, image.memory, nullptr);
    }
    image = VulkanImage{};
  }

  void destroySolidPipeline()
  {
    if (device_ == VK_NULL_HANDLE) {
      return;
    }
    if (linePipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, linePipeline_, nullptr);
      linePipeline_ = VK_NULL_HANDLE;
    }
    linePipelineRenderPass_ = VK_NULL_HANDLE;
    if (solidPipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, solidPipeline_, nullptr);
      solidPipeline_ = VK_NULL_HANDLE;
    }
    if (solidPipelineLayout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, solidPipelineLayout_, nullptr);
      solidPipelineLayout_ = VK_NULL_HANDLE;
    }
    solidPipelineRenderPass_ = VK_NULL_HANDLE;
  }

  void destroySolidResources()
  {
    destroySolidPipeline();
    destroyBuffer(cubeMesh_.vertexBuffer);
    destroyBuffer(cubeMesh_.indexBuffer);
    cubeMesh_.indexCount = 0;
    destroyBuffer(ellipsoidMesh_.vertexBuffer);
    destroyBuffer(ellipsoidMesh_.indexBuffer);
    ellipsoidMesh_.indexCount = 0;
    destroyBuffer(diskMesh_.vertexBuffer);
    destroyBuffer(diskMesh_.indexBuffer);
    diskMesh_.indexCount = 0;
    destroyBuffer(cubeInstances_.buffer);
    destroyBuffer(ellipsoidInstances_.buffer);
    destroyBuffer(diskInstances_.buffer);
    destroyBuffer(lineVertices_.buffer);
    destroyBuffer(cuboidVertices_.buffer);
    destroyBuffer(polyhedronVertices_.buffer);
    cubeInstances_ = SolidInstanceSet{};
    ellipsoidInstances_ = SolidInstanceSet{};
    diskInstances_ = SolidInstanceSet{};
    lineVertices_ = LineVertexSet{};
    cuboidVertices_ = LineVertexSet{};
    polyhedronVertices_ = LineVertexSet{};
    destroyBuffer(solidUniformBuffer_);
    if (solidDescriptorPool_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_, solidDescriptorPool_, nullptr);
      solidDescriptorPool_ = VK_NULL_HANDLE;
      solidDescriptorSet_ = VK_NULL_HANDLE;
    }
    if (solidDescriptorSetLayout_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device_, solidDescriptorSetLayout_, nullptr);
      solidDescriptorSetLayout_ = VK_NULL_HANDLE;
    }
  }

  bool ensurePipeline()
  {
    const VkRenderPass renderPass = context_.renderPass();
    if (pipelines_[0] != VK_NULL_HANDLE &&
        pipelines_[1] != VK_NULL_HANDLE &&
        pipelineRenderPass_ == renderPass) {
      return true;
    }
    destroyPipeline();
    if (renderPass == VK_NULL_HANDLE) {
      return false;
    }

    const std::vector<std::uint32_t> vert =
      ReadSpirv(ShaderPath("particle.vert.spv"));
    const std::vector<std::uint32_t> frag =
      ReadSpirv(ShaderPath("particle.frag.spv"));
    if (vert.empty() || frag.empty()) {
      return false;
    }

    VkShaderModule vertModule = createShaderModule(vert);
    VkShaderModule fragModule = createShaderModule(frag);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
      if (vertModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
      }
      if (fragModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, fragModule, nullptr);
      }
      return false;
    }

    if (!ensureDescriptorResources()) {
      return false;
    }

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;
    if (!Check(vkCreatePipelineLayout(device_,
                                      &layoutInfo,
                                      nullptr,
                                      &pipelineLayout_),
               "vkCreatePipelineLayout")) {
      vkDestroyShaderModule(device_, vertModule, nullptr);
      vkDestroyShaderModule(device_, fragModule, nullptr);
      return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(RenderParticle);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[5]{};
    attributes[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                      static_cast<std::uint32_t>(offsetof(RenderParticle, pos)) };
    attributes[1] = { 1, 0, VK_FORMAT_R8_UINT,
                      static_cast<std::uint32_t>(offsetof(RenderParticle, type)) };
    attributes[2] = {
      2,
      0,
      VK_FORMAT_R8_UINT,
      static_cast<std::uint32_t>(offsetof(RenderParticle, flag_stress))
    };
    attributes[3] = { 3, 0, VK_FORMAT_R32_SFLOAT,
                      static_cast<std::uint32_t>(offsetof(RenderParticle, hsml)) };
    attributes[4] = { 4, 0, VK_FORMAT_R32_SFLOAT,
                      static_cast<std::uint32_t>(offsetof(RenderParticle, val_show)) };

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 5;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT,
                                       VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    pipelineInfo.pDepthStencilState = &depthStencil;

    bool ok = Check(vkCreateGraphicsPipelines(device_,
                                              VK_NULL_HANDLE,
                                              1,
                                              &pipelineInfo,
                                              nullptr,
                                              &pipelines_[0]),
                    "vkCreateGraphicsPipelines(particles)");
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    ok = ok &&
         Check(vkCreateGraphicsPipelines(device_,
                                         VK_NULL_HANDLE,
                                         1,
                                         &pipelineInfo,
                                         nullptr,
                                         &pipelines_[1]),
               "vkCreateGraphicsPipelines(stress particles)");

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    if (ok) {
      pipelineRenderPass_ = renderPass;
    }
    return ok;
  }

  bool ensureObjectPipelineLayout()
  {
    if (solidPipelineLayout_ != VK_NULL_HANDLE) {
      return true;
    }
    if (!ensureSolidDescriptorResources()) {
      return false;
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(SolidPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &solidDescriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (!Check(vkCreatePipelineLayout(device_,
                                      &layoutInfo,
                                      nullptr,
                                      &solidPipelineLayout_),
               "vkCreatePipelineLayout(solid)")) {
      return false;
    }
    return true;
  }

  bool ensureSolidPipeline()
  {
    const VkRenderPass renderPass = context_.renderPass();
    if (solidPipeline_ != VK_NULL_HANDLE &&
        solidPipelineRenderPass_ == renderPass) {
      return true;
    }
    if (solidPipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, solidPipeline_, nullptr);
      solidPipeline_ = VK_NULL_HANDLE;
    }
    solidPipelineRenderPass_ = VK_NULL_HANDLE;
    if (renderPass == VK_NULL_HANDLE || !ensureObjectPipelineLayout()) {
      return false;
    }

    const std::vector<std::uint32_t> vert =
      ReadSpirv(ShaderPath("solid.vert.spv"));
    const std::vector<std::uint32_t> frag =
      ReadSpirv(ShaderPath("solid.frag.spv"));
    if (vert.empty() || frag.empty()) {
      return false;
    }

    VkShaderModule vertModule = createShaderModule(vert);
    VkShaderModule fragModule = createShaderModule(frag);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
      if (vertModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
      }
      if (fragModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, fragModule, nullptr);
      }
      return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(SolidVertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding = 1;
    bindings[1].stride = sizeof(SolidInstance);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attributes[7]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                     static_cast<std::uint32_t>(offsetof(SolidVertex, pos))};
    const std::uint32_t modelOffset =
      static_cast<std::uint32_t>(offsetof(SolidInstance, model));
    const std::uint32_t vec4Size =
      static_cast<std::uint32_t>(sizeof(glm::vec4));
    for (std::uint32_t i = 0; i < 4; ++i) {
      attributes[1 + i] = {1 + i,
                           1,
                           VK_FORMAT_R32G32B32A32_SFLOAT,
                           modelOffset + i * vec4Size};
    }
    attributes[5] = {5, 1, VK_FORMAT_R32G32B32_SFLOAT,
                     static_cast<std::uint32_t>(offsetof(SolidInstance,
                                                         color))};
    attributes[6] = {6, 1, VK_FORMAT_R32_SFLOAT,
                     static_cast<std::uint32_t>(offsetof(SolidInstance,
                                                         opacity))};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 2;
    vertexInput.pVertexBindingDescriptions = bindings;
    vertexInput.vertexAttributeDescriptionCount =
      static_cast<std::uint32_t>(std::size(attributes));
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                      VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = solidPipelineLayout_;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    const bool ok = Check(vkCreateGraphicsPipelines(device_,
                                                    VK_NULL_HANDLE,
                                                    1,
                                                    &pipelineInfo,
                                                    nullptr,
                                                    &solidPipeline_),
                          "vkCreateGraphicsPipelines(solid)");
    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);
    if (ok) {
      solidPipelineRenderPass_ = renderPass;
    }
    return ok;
  }

  bool ensureLinePipeline()
  {
    const VkRenderPass renderPass = context_.renderPass();
    if (linePipeline_ != VK_NULL_HANDLE &&
        linePipelineRenderPass_ == renderPass) {
      return true;
    }
    if (linePipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, linePipeline_, nullptr);
      linePipeline_ = VK_NULL_HANDLE;
    }
    linePipelineRenderPass_ = VK_NULL_HANDLE;
    if (renderPass == VK_NULL_HANDLE || !ensureObjectPipelineLayout()) {
      return false;
    }

    const std::vector<std::uint32_t> vert =
      ReadSpirv(ShaderPath("line.vert.spv"));
    const std::vector<std::uint32_t> frag =
      ReadSpirv(ShaderPath("line.frag.spv"));
    if (vert.empty() || frag.empty()) {
      return false;
    }

    VkShaderModule vertModule = createShaderModule(vert);
    VkShaderModule fragModule = createShaderModule(frag);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
      if (vertModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
      }
      if (fragModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, fragModule, nullptr);
      }
      return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(LineVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[2]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                     static_cast<std::uint32_t>(offsetof(LineVertex, pos))};
    attributes[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                     static_cast<std::uint32_t>(offsetof(LineVertex, color))};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount =
      static_cast<std::uint32_t>(std::size(attributes));
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                      VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = solidPipelineLayout_;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    const bool ok = Check(vkCreateGraphicsPipelines(device_,
                                                    VK_NULL_HANDLE,
                                                    1,
                                                    &pipelineInfo,
                                                    nullptr,
                                                    &linePipeline_),
                          "vkCreateGraphicsPipelines(line)");
    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);
    if (ok) {
      linePipelineRenderPass_ = renderPass;
    }
    return ok;
  }

  VkShaderModule createShaderModule(const std::vector<std::uint32_t>& code)
  {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(std::uint32_t);
    createInfo.pCode = code.data();

    VkShaderModule module = VK_NULL_HANDLE;
    Check(vkCreateShaderModule(device_, &createInfo, nullptr, &module),
          "vkCreateShaderModule");
    return module;
  }

  void destroyPipeline()
  {
    if (device_ == VK_NULL_HANDLE) {
      return;
    }
    destroySolidPipeline();
    for (VkPipeline& pipeline : pipelines_) {
      if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
      }
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
      pipelineLayout_ = VK_NULL_HANDLE;
    }
    pipelineRenderPass_ = VK_NULL_HANDLE;
  }

  void draw(VkCommandBuffer commandBuffer)
  {
    if (!frame_.valid || !ensurePipeline()) {
      return;
    }
    if (visualDirty_) {
      syncVisualUniform(0, false);
      syncVisualUniform(1, true);
    }

    const VkExtent2D extent = context_.swapchainExtent();
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    drawParticleBuffer(commandBuffer, particleBuffer_, particleCount_, 0);
    drawParticleBuffer(commandBuffer,
                       stressParticleBuffer_,
                       stressParticleCount_,
                       1);
    drawSolidLayer(commandBuffer,
                   ellipsoidMesh_,
                   ellipsoidInstances_,
                   frame_.runtime.ellipsoids);
    drawSolidLayer(commandBuffer,
                   diskMesh_,
                   diskInstances_,
                   frame_.runtime.disks);
    drawSolidLayer(commandBuffer,
                   cubeMesh_,
                   cubeInstances_,
                   frame_.runtime.cubes);
    drawLineSet(commandBuffer, cuboidVertices_, frame_.runtime.cuboids);
    drawLineSet(commandBuffer, lineVertices_, frame_.runtime.lines);
    drawLineSet(commandBuffer, polyhedronVertices_, frame_.runtime.polyhedra);
  }

  void drawParticleBuffer(VkCommandBuffer commandBuffer,
                          const VulkanBuffer& buffer,
                          std::size_t count,
                          std::size_t passIndex)
  {
    if (count == 0 || buffer.buffer == VK_NULL_HANDLE ||
        passIndex >= pipelines_.size() ||
        pipelines_[passIndex] == VK_NULL_HANDLE ||
        descriptorSets_[passIndex] == VK_NULL_HANDLE) {
      return;
    }

    VkDeviceSize offsets[] = { 0 };
    vkCmdBindPipeline(commandBuffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipelines_[passIndex]);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &buffer.buffer, offsets);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_,
                            0,
                            1,
                            &descriptorSets_[passIndex],
                            0,
                            nullptr);

    vkCmdDraw(commandBuffer, static_cast<std::uint32_t>(count), 1, 0, 0);
  }

  void drawSolidLayer(VkCommandBuffer commandBuffer,
                      const SolidMesh& mesh,
                      const SolidInstanceSet& instances,
                      const RenderLayerState& runtime)
  {
    if (!runtime.show || instances.count == 0 ||
        mesh.vertexBuffer.buffer == VK_NULL_HANDLE ||
        mesh.indexBuffer.buffer == VK_NULL_HANDLE ||
        mesh.indexCount == 0 ||
        instances.buffer.buffer == VK_NULL_HANDLE ||
        !ensureSolidPipeline()) {
      return;
    }

    VkDeviceSize offsets[] = {0, 0};
    VkBuffer vertexBuffers[] = {mesh.vertexBuffer.buffer,
                                instances.buffer.buffer};
    vkCmdBindPipeline(commandBuffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      solidPipeline_);
    vkCmdBindVertexBuffers(commandBuffer, 0, 2, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer,
                         mesh.indexBuffer.buffer,
                         0,
                         VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            solidPipelineLayout_,
                            0,
                            1,
                            &solidDescriptorSet_,
                            0,
                            nullptr);

    SolidPushConstants push;
    push.opacityScale = std::clamp(runtime.opacity, 0.0f, 1.0f);
    vkCmdPushConstants(commandBuffer,
                       solidPipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0,
                       sizeof(push),
                       &push);

    vkCmdDrawIndexed(commandBuffer,
                     mesh.indexCount,
                     static_cast<std::uint32_t>(instances.count),
                     0,
                     0,
                     0);
  }

  void drawLineSet(VkCommandBuffer commandBuffer,
                   const LineVertexSet& vertices,
                   const RenderLayerState& runtime)
  {
    if (!runtime.show || vertices.count == 0 ||
        vertices.buffer.buffer == VK_NULL_HANDLE || !ensureLinePipeline()) {
      return;
    }

    VkDeviceSize offsets[] = {0};
    vkCmdBindPipeline(commandBuffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      linePipeline_);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer.buffer, offsets);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            solidPipelineLayout_,
                            0,
                            1,
                            &solidDescriptorSet_,
                            0,
                            nullptr);

    SolidPushConstants push;
    push.opacityScale = std::clamp(runtime.opacity, 0.0f, 1.0f);
    vkCmdPushConstants(commandBuffer,
                       solidPipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0,
                       sizeof(push),
                       &push);
    vkCmdDraw(commandBuffer,
              static_cast<std::uint32_t>(vertices.count),
              1,
              0,
              0);
  }

  VulkanContext& context_;
  VkDevice device_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VulkanBuffer particleBuffer_;
  VulkanBuffer stressParticleBuffer_;
  std::array<VulkanBuffer, 2> visualBuffers_;
  VulkanImage colormapAtlas_;
  VkSampler colormapSampler_ = VK_NULL_HANDLE;
  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  std::size_t particleCount_ = 0;
  std::size_t stressParticleCount_ = 0;
  RenderSceneVersion particleVersion_ = 0;
  RenderSceneVersion stressParticleVersion_ = 0;
  RenderFrameState frame_;
  std::array<float, 6> pointSizes_ = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  std::array<float, 6> valueMin_ = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  std::array<float, 6> valueMax_ = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  std::array<std::uint32_t, 6> colormap_ = {0u, 0u, 0u, 0u, 0u, 0u};
  std::uint32_t hiddenTypeMask_ = 0;
  std::uint32_t logTypeMask_ = 0;
  std::uint32_t periodicTypeMask_ = 0;
  bool visualDirty_ = true;
  VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
  std::array<VkDescriptorSet, 2> descriptorSets_ = {
    VK_NULL_HANDLE,
    VK_NULL_HANDLE
  };
  VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
  std::array<VkPipeline, 2> pipelines_ = {VK_NULL_HANDLE, VK_NULL_HANDLE};
  VkRenderPass pipelineRenderPass_ = VK_NULL_HANDLE;
  SolidMesh cubeMesh_;
  SolidMesh ellipsoidMesh_;
  SolidMesh diskMesh_;
  SolidInstanceSet cubeInstances_;
  SolidInstanceSet ellipsoidInstances_;
  SolidInstanceSet diskInstances_;
  LineVertexSet lineVertices_;
  LineVertexSet cuboidVertices_;
  LineVertexSet polyhedronVertices_;
  VulkanBuffer solidUniformBuffer_;
  VkDescriptorSetLayout solidDescriptorSetLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool solidDescriptorPool_ = VK_NULL_HANDLE;
  VkDescriptorSet solidDescriptorSet_ = VK_NULL_HANDLE;
  VkPipelineLayout solidPipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline solidPipeline_ = VK_NULL_HANDLE;
  VkRenderPass solidPipelineRenderPass_ = VK_NULL_HANDLE;
  VkPipeline linePipeline_ = VK_NULL_HANDLE;
  VkRenderPass linePipelineRenderPass_ = VK_NULL_HANDLE;
  VulkanImage previewImage_;
  VkSampler previewSampler_ = VK_NULL_HANDLE;
  VkDescriptorSet previewDescriptorSet_ = VK_NULL_HANDLE;
  std::uint64_t previewVersion_ = 0;
  ProjectionPreviewUIState preview_;
};

std::unique_ptr<RenderBackend> CreateVulkanRenderBackend()
{
  std::cerr << "Vulkan render backend requires a Vulkan platform context; "
               "using null backend."
            << std::endl;
  return CreateNullRenderBackend();
}

std::unique_ptr<RenderBackend> CreateVulkanRenderBackend(VulkanContext& context)
{
  return std::make_unique<VulkanRenderBackend>(context);
}
