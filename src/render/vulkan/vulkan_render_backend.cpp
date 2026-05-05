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
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <cmath>
#include <cstdio>
#include <type_traits>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <memory>
#include <utility>
#include <string>
#include <string_view>
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

std::string LowerCopy(std::string text)
{
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

bool LooksLikeSoftwareVulkanDevice(VkPhysicalDeviceType type,
                                   std::string_view name)
{
  if (type == VK_PHYSICAL_DEVICE_TYPE_CPU) {
    return true;
  }
  const std::string lower = LowerCopy(std::string(name));
  return lower.find("llvmpipe") != std::string::npos ||
         lower.find("lavapipe") != std::string::npos ||
         lower.find("softpipe") != std::string::npos ||
         lower.find("software") != std::string::npos;
}

bool ShouldUseParticleLod(const RenderRuntimeState& render,
                          const std::vector<RenderParticle>& proxy,
                          bool softwareRenderer)
{
  if (proxy.empty()) {
    return false;
  }

  switch (render.scheduling.particleLod.mode) {
    case ParticleLodMode::Off:
      return render.scheduling.autoParticleLodOnSoftwareRenderer &&
             softwareRenderer &&
             render.scheduling.interactionActive;
    case ParticleLodMode::WhileInteracting:
      return render.scheduling.interactionActive;
    case ParticleLodMode::Always:
      return true;
  }
  return false;
}

bool HasDeviceExtension(VkPhysicalDevice device, const char* extension)
{
  std::uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> properties(count);
  vkEnumerateDeviceExtensionProperties(device,
                                       nullptr,
                                       &count,
                                       properties.data());
  return std::any_of(properties.begin(),
                     properties.end(),
                     [&](const VkExtensionProperties& prop) {
                       return std::strcmp(prop.extensionName, extension) == 0;
                     });
}

bool EqualMatrix(const glm::mat4& a, const glm::mat4& b)
{
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      if (a[c][r] != b[c][r]) {
        return false;
      }
    }
  }
  return true;
}

bool EqualParticleTypeVisualConfig(const ParticleTypeVisualConfig& a,
                                   const ParticleTypeVisualConfig& b)
{
  return a.selectedQuantity == b.selectedQuantity &&
         a.pointSize == b.pointSize &&
         a.colorMin == b.colorMin &&
         a.colorMax == b.colorMax &&
         a.useLogScale == b.useLogScale &&
         a.hideParticles == b.hideParticles &&
         a.periodicColorBar == b.periodicColorBar &&
         a.colormapIndex == b.colormapIndex;
}

bool EqualParticleVisualConfig(const ParticleVisualConfig& a,
                               const ParticleVisualConfig& b)
{
  for (int i = 0; i < kNumParticleTypes; ++i) {
    if (!EqualParticleTypeVisualConfig(a.types[i], b.types[i])) {
      return false;
    }
  }
  return true;
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

struct VulkanParticleLodTreeBuffers {
  VulkanBuffer nodeCenterRadius;
  VulkanBuffer representativePosHsml;
  VulkanBuffer representativeValue;
  VulkanBuffer nodeMeta;
  VulkanBuffer childA;
  VulkanBuffer childB;
  VulkanBuffer representativeMeta;
  VulkanBuffer indices;
  std::size_t nodeCount = 0;
  std::size_t indexCount = 0;
  RenderSceneVersion version = 0;
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

#ifdef VOLUME_RENDERING
constexpr std::size_t kVolumeTransferGroups = 4;
constexpr std::size_t kVolumeTransferSlots = kVolumeTransferGroups * 4;

struct VolumeUniform {
  glm::mat4 invProjection{1.0f};
  glm::mat4 invView{1.0f};
  glm::vec4 cameraForwardFocal{0.0f, 0.0f, -1.0f, 1.0f};
  glm::vec4 rayParams{2.0f, 1.0f, 0.0f, 1.0e-4f};
  glm::vec4 baseColorAndMode{0.6f, 0.7f, 1.0f, 0.0f};
  glm::vec4 tfRangeScale{1.0e-6f, 1.0f, 1.0f, 0.0f};
  glm::ivec4 tfControl{1, 0, -1, 0};
  glm::ivec4 colorControl{1, 1, 0, 0};
  glm::vec4 opticalParams{0.0f, 1.0f, 1.0f, 0.0f};
  std::array<glm::ivec4, kVolumeTransferGroups> tfType{};
  std::array<glm::ivec4, kVolumeTransferGroups> tfLogDomain{};
  std::array<glm::vec4, kVolumeTransferGroups> tfCenter{};
  std::array<glm::vec4, kVolumeTransferGroups> tfWidth{};
  std::array<glm::vec4, kVolumeTransferGroups> tfAmp{};
};

struct VulkanVolumeFrameCache {
  VulkanImage image;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkRenderPass pipelineRenderPass = VK_NULL_HANDLE;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  RenderSceneVersion volumeVersion = 0;
  VolumeUniform uniform;
  bool uniformInitialized = false;
  bool imageInitialized = false;
  bool valid = false;
};
#endif

struct VulkanParticleFrameCache {
  VulkanImage color;
  VulkanImage depth;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkRenderPass pipelineRenderPass = VK_NULL_HANDLE;
  std::array<VkPipeline, 2> particlePipelines = {VK_NULL_HANDLE, VK_NULL_HANDLE};
  VkRenderPass particlePipelineRenderPass = VK_NULL_HANDLE;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  RenderSceneVersion particlesVersion = 0;
  glm::mat4 model{1.0f};
  glm::mat4 view{1.0f};
  glm::mat4 projection{1.0f};
  ParticleVisualConfig visualConfig;
  bool imageInitialized = false;
  bool valid = false;
};

constexpr std::uint32_t kColormapAtlasWidth = 256;

float Lerp(float a, float b, float t)
{
  return a + (b - a) * t;
}

float HalfToFloat(std::uint16_t h)
{
  const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000u) << 16;
  std::uint32_t exp = (h >> 10) & 0x1fu;
  std::uint32_t mant = h & 0x03ffu;
  std::uint32_t bits = 0;
  if (exp == 0) {
    if (mant == 0) {
      bits = sign;
    } else {
      exp = 1;
      while ((mant & 0x0400u) == 0) {
        mant <<= 1;
        --exp;
      }
      mant &= 0x03ffu;
      bits = sign | ((exp + 127u - 15u) << 23) | (mant << 13);
    }
  } else if (exp == 31u) {
    bits = sign | 0x7f800000u | (mant << 13);
  } else {
    bits = sign | ((exp + 127u - 15u) << 23) | (mant << 13);
  }
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
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

#ifdef ISO_CONTOUR
SolidMeshData BuildIsoContourMeshData(const IsoContourRenderData& data)
{
  SolidMeshData mesh;
  mesh.vertices.reserve(data.verts.size() / 3u);
  for (std::size_t i = 0; i + 2 < data.verts.size(); i += 3) {
    mesh.vertices.push_back(
      {{data.verts[i + 0], data.verts[i + 1], data.verts[i + 2]}});
  }
  mesh.indices.reserve(data.inds.size());
  for (unsigned index : data.inds) {
    mesh.indices.push_back(static_cast<std::uint32_t>(index));
  }
  return mesh;
}
#endif

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
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    softwareRenderer_ =
      LooksLikeSoftwareVulkanDevice(props.deviceType, props.deviceName);
#ifdef VK_EXT_MEMORY_BUDGET_EXTENSION_NAME
    memoryBudgetSupported_ =
      HasDeviceExtension(physicalDevice_, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
#endif
#ifdef VOLUME_RENDERING
    timestampPeriodNs_ = props.limits.timestampPeriod;
#endif
    createCommandPool();
    context_.setPreRenderCallback([this](VkCommandBuffer commandBuffer) {
      updateParticleFrameCache(commandBuffer);
      updateVolumeFrameCache(commandBuffer);
    });
    context_.setPreImGuiDrawCallback([this](VkCommandBuffer commandBuffer) {
      draw(commandBuffer);
    });
  }

  void destroy() override
  {
    context_.setPreRenderCallback({});
    context_.setPreImGuiDrawCallback({});
    if (device_ != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(device_);
    }
    destroyPipeline();
    destroySolidResources();
#ifdef VOLUME_RENDERING
    destroyVolumeResources();
#endif
    destroyProjectionPreviewResources();
    destroyParticleFrameCache();
    destroyBuffer(particleBuffer_);
    destroyBuffer(stressParticleBuffer_);
    destroyBuffer(particleLodBuffer_);
    destroyBuffer(stressParticleLodBuffer_);
    destroyParticleLodTreeBuffers();
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
    useParticleLod_ =
      ShouldUseParticleLod(frame.runtime, scene.particleLodProxy, softwareRenderer_);
    if (useParticleLod_) {
      syncParticleBuffer(scene.particleLodProxy,
                         scene.particleLodVersion,
                         particleLodBuffer_,
                         particleLodVersion_,
                         particleLodCount_,
                         "vkMapMemory(particle LOD)");
      syncParticleBuffer(scene.particleLodStressProxy,
                         scene.particleLodVersion,
                         stressParticleLodBuffer_,
                         stressParticleLodVersion_,
                         stressParticleLodCount_,
                         "vkMapMemory(stress particle LOD)");
      syncParticleLodTree(scene);
    }
    syncSolidInstances(scene);
    syncLineVertices(scene);
#ifdef VOLUME_RENDERING
    syncVolume(scene);
#endif
    syncVisualUniform(0, false);
    syncVisualUniform(1, true);
    syncSolidUniform();
    frame_.overlay.particleLabels.draw(frame_.matrices.view,
                                       frame_.matrices.projection,
                                       frame_.viewport);
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
    caps.particleLod = true;
    caps.velocityField = true;
    caps.particleFrameCache = true;
#ifdef ISO_CONTOUR
    caps.isoContour = true;
#endif
#ifdef VK_EXT_MEMORY_BUDGET_EXTENSION_NAME
    caps.gpuMemoryQuery = memoryBudgetSupported_;
#endif
#ifdef VOLUME_RENDERING
    caps.volumeRendering = true;
    caps.volumeFrameCache = true;
#endif
    return caps;
  }

  RenderBackendMemoryInfo queryMemoryInfo() const override
  {
    RenderBackendMemoryInfo info;
#ifdef VK_EXT_MEMORY_BUDGET_EXTENSION_NAME
    if (!memoryBudgetSupported_ || physicalDevice_ == VK_NULL_HANDLE) {
      return info;
    }

    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
    budget.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
    VkPhysicalDeviceMemoryProperties2 memoryProps{};
    memoryProps.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    memoryProps.pNext = &budget;
    vkGetPhysicalDeviceMemoryProperties2(physicalDevice_, &memoryProps);

    std::size_t available = 0;
    for (std::uint32_t i = 0; i < memoryProps.memoryProperties.memoryHeapCount;
         ++i) {
      const VkMemoryHeap& heap = memoryProps.memoryProperties.memoryHeaps[i];
      if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) {
        continue;
      }
      const VkDeviceSize heapAvailable =
        budget.heapBudget[i] > budget.heapUsage[i]
          ? budget.heapBudget[i] - budget.heapUsage[i]
          : 0;
      available += static_cast<std::size_t>(heapAvailable);
    }
    info.gpuAvailableKnown = true;
    info.gpuAvailableBytes = available;
#endif
    return info;
  }

  RenderBackendTimingInfo queryTimingInfo() const override
  {
    return timing_;
  }

#ifdef VOLUME_RENDERING
  RenderBackendVolumeStats queryVolumeStats(int sampleStep) override
  {
    RenderBackendVolumeStats stats;
    sampleStep = std::max(1, sampleStep);
    if (!volumeCanDraw()) {
      return stats;
    }

    const VkExtent2D extent = context_.swapchainExtent();
    if (extent.width == 0 || extent.height == 0) {
      return stats;
    }
    const std::uint32_t width =
      std::max(1u, extent.width / static_cast<std::uint32_t>(sampleStep));
    const std::uint32_t height =
      std::max(1u, extent.height / static_cast<std::uint32_t>(sampleStep));
    const float scale = static_cast<float>(height) /
                        static_cast<float>(std::max(1u, extent.height));

    if (!ensureVolumeFrameCacheTarget(width, height) ||
        !ensureVolumeRayPipeline(volumeFrameCache_.renderPass,
                                 volumeStatsPipeline_,
                                 volumeStatsPipelineRenderPass_,
                                 false)) {
      return stats;
    }

    VolumeUniform uniform = buildVolumeUniform(scale, 20);
    uniform.tfControl.w = 20;
    if (!uploadVolumeUniform(uniform)) {
      return stats;
    }

    VkCommandBuffer commandBuffer = beginImmediateCommands();
    if (commandBuffer == VK_NULL_HANDLE) {
      return stats;
    }

    recordImageLayoutTransition(
      commandBuffer,
      volumeFrameCache_.image.image,
      volumeFrameCache_.imageInitialized
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    volumeFrameCache_.imageInitialized = true;

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = volumeFrameCache_.renderPass;
    renderPassInfo.framebuffer = volumeFrameCache_.framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {width, height};
    VkClearValue clearValue{};
    clearValue.color.float32[0] = 0.0f;
    clearValue.color.float32[1] = 0.0f;
    clearValue.color.float32[2] = 0.0f;
    clearValue.color.float32[3] = 0.0f;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(commandBuffer,
                         &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    recordVolumeViewportAndScissor(commandBuffer, width, height);
    vkCmdBindPipeline(commandBuffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      volumeStatsPipeline_);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            volumePipelineLayout_,
                            0,
                            1,
                            &volumeDescriptorSet_,
                            0,
                            nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    recordImageLayoutTransition(commandBuffer,
                                volumeFrameCache_.image.image,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    const VkDeviceSize bytes =
      static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) *
      4u * sizeof(std::uint16_t);
    VulkanBuffer readback = createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    if (readback.buffer == VK_NULL_HANDLE) {
      vkEndCommandBuffer(commandBuffer);
      vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
      return stats;
    }

    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(commandBuffer,
                           volumeFrameCache_.image.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback.buffer,
                           1,
                           &copy);

    recordImageLayoutTransition(commandBuffer,
                                volumeFrameCache_.image.image,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    const bool submitted = endImmediateCommands(commandBuffer);
    if (submitted) {
      void* mapped = nullptr;
      if (Check(vkMapMemory(device_, readback.memory, 0, bytes, 0, &mapped),
                "vkMapMemory(volume stats readback)")) {
        const auto* half =
          static_cast<const std::uint16_t*>(mapped);
        const std::size_t count =
          static_cast<std::size_t>(width) *
          static_cast<std::size_t>(height);
        double nodeVisits = 0.0;
        double childHits = 0.0;
        double leafStops = 0.0;
        double rootHits = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
          const float visits = HalfToFloat(half[4 * i + 0]);
          nodeVisits += visits;
          childHits += HalfToFloat(half[4 * i + 1]);
          leafStops += HalfToFloat(half[4 * i + 2]);
          if (visits > 0.0f) {
            rootHits += 1.0;
          }
        }
        vkUnmapMemory(device_, readback.memory);

        const double sampledRays = static_cast<double>(count);
        stats.known = true;
        stats.width = static_cast<int>(width);
        stats.height = static_cast<int>(height);
        stats.sampleStep = sampleStep;
        stats.sampledRays = sampledRays;
        stats.rootHitFraction = sampledRays > 0.0 ? rootHits / sampledRays : 0.0;
        stats.nodeVisits = nodeVisits;
        stats.childHits = childHits;
        stats.leafStops = leafStops;
        stats.avgNodeVisitsPerRay =
          sampledRays > 0.0 ? nodeVisits / sampledRays : 0.0;
        stats.avgChildHitsPerRay =
          sampledRays > 0.0 ? childHits / sampledRays : 0.0;
        stats.avgLeafStopsPerRay =
          sampledRays > 0.0 ? leafStops / sampledRays : 0.0;
      }
    }
    destroyBuffer(readback);
    volumeFrameCache_.valid = false;
    return stats;
  }
#endif

  void waitIdle() override
  {
    if (device_ != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(device_);
    }
#ifdef VOLUME_RENDERING
    collectVolumeTimingQueryResult();
#endif
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
    syncParticleBuffer(scene.particles,
                       scene.particlesVersion,
                       particleBuffer_,
                       particleVersion_,
                       particleCount_,
                       "vkMapMemory(particles)");
  }

  void syncStressParticles(const RenderSceneData& scene)
  {
    syncParticleBuffer(scene.stressParticles,
                       scene.stressParticlesVersion,
                       stressParticleBuffer_,
                       stressParticleVersion_,
                       stressParticleCount_,
                       "vkMapMemory(stress particles)");
  }

  void syncParticleBuffer(const std::vector<RenderParticle>& particles,
                          RenderSceneVersion version,
                          VulkanBuffer& buffer,
                          RenderSceneVersion& uploadedVersion,
                          std::size_t& count,
                          const char* label)
  {
    if (version == uploadedVersion) {
      count = particles.size();
      return;
    }

    uploadedVersion = version;
    count = particles.size();
    const VkDeviceSize bytes =
      static_cast<VkDeviceSize>(particles.size() * sizeof(RenderParticle));
    if (bytes == 0) {
      return;
    }
    if (buffer.size < bytes) {
      destroyBuffer(buffer);
      buffer = createBuffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
    if (buffer.memory == VK_NULL_HANDLE) {
      return;
    }

    void* mapped = nullptr;
    if (!Check(vkMapMemory(device_,
                           buffer.memory,
                           0,
                           bytes,
                           0,
                           &mapped),
               label)) {
      return;
    }
    std::memcpy(mapped, particles.data(), static_cast<std::size_t>(bytes));
    vkUnmapMemory(device_, buffer.memory);
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

  template <typename T>
  bool uploadStorageVector(const std::vector<T>& values,
                           VulkanBuffer& buffer,
                           const char* label)
  {
    const VkDeviceSize bytes =
      static_cast<VkDeviceSize>(values.size() * sizeof(T));
    if (bytes == 0) {
      destroyBuffer(buffer);
      return true;
    }
    return uploadToBuffer(buffer,
                          values.data(),
                          bytes,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          label);
  }

  void syncParticleLodTree(const RenderSceneData& scene)
  {
    if (particleLodTreeBuffers_.version == scene.particleLodVersion) {
      return;
    }

    const ParticleLodGpuTree& tree = scene.particleLodGpu;
    if (!tree.valid || tree.nodeCenterRadius.empty()) {
      destroyParticleLodTreeBuffers();
      return;
    }

    bool ok = true;
    ok &= uploadStorageVector(tree.nodeCenterRadius,
                              particleLodTreeBuffers_.nodeCenterRadius,
                              "vkMapMemory(particle LOD node center)");
    ok &= uploadStorageVector(tree.representativePosHsml,
                              particleLodTreeBuffers_.representativePosHsml,
                              "vkMapMemory(particle LOD representative pos)");
    ok &= uploadStorageVector(tree.representativeValue,
                              particleLodTreeBuffers_.representativeValue,
                              "vkMapMemory(particle LOD representative value)");
    ok &= uploadStorageVector(tree.nodeMeta,
                              particleLodTreeBuffers_.nodeMeta,
                              "vkMapMemory(particle LOD node meta)");
    ok &= uploadStorageVector(tree.childA,
                              particleLodTreeBuffers_.childA,
                              "vkMapMemory(particle LOD child A)");
    ok &= uploadStorageVector(tree.childB,
                              particleLodTreeBuffers_.childB,
                              "vkMapMemory(particle LOD child B)");
    ok &= uploadStorageVector(tree.representativeMeta,
                              particleLodTreeBuffers_.representativeMeta,
                              "vkMapMemory(particle LOD representative meta)");
    ok &= uploadStorageVector(tree.indices,
                              particleLodTreeBuffers_.indices,
                              "vkMapMemory(particle LOD indices)");

    if (!ok) {
      destroyParticleLodTreeBuffers();
      return;
    }

    particleLodTreeBuffers_.nodeCount = tree.nodeCenterRadius.size();
    particleLodTreeBuffers_.indexCount = tree.indices.size();
    particleLodTreeBuffers_.version = scene.particleLodVersion;
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
#ifdef ISO_CONTOUR
    syncIsoContour(scene);
#endif
  }

#ifdef ISO_CONTOUR
  void syncIsoContour(const RenderSceneData& scene)
  {
    if (isoContourVersion_ == scene.isoContourVersion) {
      return;
    }
    isoContourVersion_ = scene.isoContourVersion;

    const SolidMeshData meshData = BuildIsoContourMeshData(scene.isoContour);
    if (meshData.vertices.empty() || meshData.indices.empty()) {
      isoContourMesh_.indexCount = 0;
      isoContourInstances_.count = 0;
      return;
    }
    if (!uploadSolidMesh(isoContourMesh_, meshData)) {
      isoContourMesh_.indexCount = 0;
      isoContourInstances_.count = 0;
      return;
    }

    const SolidInstance instance{glm::mat4(1.0f),
                                 glm::vec3(1.0f),
                                 1.0f};
    uploadToBuffer(isoContourInstances_.buffer,
                   &instance,
                   sizeof(instance),
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                   "vkMapMemory(isocontour instance)");
    isoContourInstances_.count = 1;
    isoContourInstances_.version = scene.isoContourVersion;
  }
#endif

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

  static std::vector<LineVertex> BuildVelocityVertices(
    const std::vector<float>& instances,
    float scaleFactor,
    bool useLogScale)
  {
    std::vector<LineVertex> vertices;
    vertices.reserve((instances.size() / 6u) * 6u);
    const glm::vec4 color(1.0f, 0.0f, 0.0f, 1.0f);
    for (std::size_t i = 0; i + 5 < instances.size(); i += 6) {
      const glm::vec3 pos(instances[i + 0], instances[i + 1], instances[i + 2]);
      const glm::vec3 vel(instances[i + 3], instances[i + 4], instances[i + 5]);
      const float speed = glm::length(vel);
      if (speed <= 1.0e-6f) {
        continue;
      }

      const float magnitude = useLogScale ? std::log(speed + 1.0f) : speed;
      const float length = magnitude * scaleFactor;
      if (length <= 0.0f) {
        continue;
      }

      const glm::vec3 dir = vel / speed;
      const glm::vec3 ref =
        std::abs(dir.z) < 0.95f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                : glm::vec3(0.0f, 1.0f, 0.0f);
      const glm::vec3 side = glm::normalize(glm::cross(ref, dir));
      const glm::vec3 tip = pos + dir * length;
      const glm::vec3 neck = pos + dir * (0.82f * length);
      const glm::vec3 headOffset = side * (0.08f * length);
      AppendLinePair(vertices, pos, tip, color);
      AppendLinePair(vertices, tip, neck + headOffset, color);
      AppendLinePair(vertices, tip, neck - headOffset, color);
    }
    return vertices;
  }

  void syncVelocityVertices(const RenderSceneData& scene,
                            const VelocityRenderState& runtime)
  {
    if (velocityVersion_ == scene.velocityVersion &&
        velocityArrowScale_ == runtime.arrowScale &&
        velocityUseLogScale_ == runtime.useLogScale) {
      velocityVertices_.count = velocityVertices_.buffer.buffer != VK_NULL_HANDLE
        ? velocityVertices_.count
        : 0;
      return;
    }

    const std::vector<LineVertex> vertices =
      BuildVelocityVertices(scene.velocityInstances,
                            runtime.arrowScale,
                            runtime.useLogScale);
    uploadToBuffer(velocityVertices_.buffer,
                   vertices.data(),
                   static_cast<VkDeviceSize>(vertices.size() *
                                             sizeof(LineVertex)),
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                   "vkMapMemory(velocity vertices)");
    velocityVertices_.count = vertices.size();
    velocityVersion_ = scene.velocityVersion;
    velocityArrowScale_ = runtime.arrowScale;
    velocityUseLogScale_ = runtime.useLogScale;
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
    syncVelocityVertices(scene, frame_.runtime.velocity);
  }

#ifdef VOLUME_RENDERING
  void updateVolumeDescriptorSet()
  {
    if (volumeDescriptorSet_ == VK_NULL_HANDLE ||
        volumeUniformBuffer_.buffer == VK_NULL_HANDLE ||
        volumeNodeMinBuffer_.buffer == VK_NULL_HANDLE ||
        volumeNodeMaxBuffer_.buffer == VK_NULL_HANDLE ||
        volumeChildABuffer_.buffer == VK_NULL_HANDLE ||
        volumeChildBBuffer_.buffer == VK_NULL_HANDLE ||
        volumeCornerLoBuffer_.buffer == VK_NULL_HANDLE ||
        volumeCornerHiBuffer_.buffer == VK_NULL_HANDLE ||
        colormapAtlas_.view == VK_NULL_HANDLE ||
        colormapSampler_ == VK_NULL_HANDLE) {
      return;
    }

    VkDescriptorBufferInfo uniformInfo{};
    uniformInfo.buffer = volumeUniformBuffer_.buffer;
    uniformInfo.offset = 0;
    uniformInfo.range = sizeof(VolumeUniform);

    std::array<VkDescriptorBufferInfo, 6> nodeInfos{};
    VulkanBuffer* buffers[6] = {
      &volumeNodeMinBuffer_,
      &volumeNodeMaxBuffer_,
      &volumeChildABuffer_,
      &volumeChildBBuffer_,
      &volumeCornerLoBuffer_,
      &volumeCornerHiBuffer_
    };
    for (std::size_t i = 0; i < nodeInfos.size(); ++i) {
      nodeInfos[i].buffer = buffers[i]->buffer;
      nodeInfos[i].offset = 0;
      nodeInfos[i].range = VK_WHOLE_SIZE;
    }

    VkDescriptorImageInfo colormapInfo{};
    colormapInfo.sampler = colormapSampler_;
    colormapInfo.imageView = colormapAtlas_.view;
    colormapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 8> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = volumeDescriptorSet_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &uniformInfo;
    for (std::size_t i = 0; i < nodeInfos.size(); ++i) {
      writes[i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i + 1].dstSet = volumeDescriptorSet_;
      writes[i + 1].dstBinding = static_cast<std::uint32_t>(i + 1);
      writes[i + 1].descriptorCount = 1;
      writes[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[i + 1].pBufferInfo = &nodeInfos[i];
    }
    writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[7].dstSet = volumeDescriptorSet_;
    writes[7].dstBinding = 7;
    writes[7].descriptorCount = 1;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[7].pImageInfo = &colormapInfo;
    vkUpdateDescriptorSets(device_,
                           static_cast<std::uint32_t>(writes.size()),
                           writes.data(),
                           0,
                           nullptr);
    volumeDescriptorReady_ = true;
  }

  bool ensureVolumeDescriptorResources()
  {
    if (volumeDescriptorSetLayout_ != VK_NULL_HANDLE) {
      return volumeDescriptorSet_ != VK_NULL_HANDLE &&
             volumeUniformBuffer_.buffer != VK_NULL_HANDLE;
    }
    if (!ensureColormapAtlas()) {
      return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 8> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    for (std::size_t i = 1; i <= 6; ++i) {
      bindings[i].binding = static_cast<std::uint32_t>(i);
      bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[i].descriptorCount = 1;
      bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (!Check(vkCreateDescriptorSetLayout(device_,
                                           &layoutInfo,
                                           nullptr,
                                           &volumeDescriptorSetLayout_),
               "vkCreateDescriptorSetLayout(volume)")) {
      return false;
    }

    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 6;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[2].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (!Check(vkCreateDescriptorPool(device_,
                                      &poolInfo,
                                      nullptr,
                                      &volumeDescriptorPool_),
               "vkCreateDescriptorPool(volume)")) {
      return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = volumeDescriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &volumeDescriptorSetLayout_;
    if (!Check(vkAllocateDescriptorSets(device_,
                                        &allocInfo,
                                        &volumeDescriptorSet_),
               "vkAllocateDescriptorSets(volume)")) {
      return false;
    }

    volumeUniformBuffer_ = createBuffer(sizeof(VolumeUniform),
                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    return volumeUniformBuffer_.buffer != VK_NULL_HANDLE;
  }

  void syncVolume(const RenderSceneData& scene)
  {
    if (!scene.volume.valid()) {
      volumeNodeCount_ = 0;
      volumeRoot_ = -1;
      volumeUploadedVersion_ = scene.volumeVersion;
      volumeDescriptorReady_ = false;
      volumeFrameCache_.valid = false;
      return;
    }
    if (volumeUploadedVersion_ == scene.volumeVersion &&
        volumeNodeCount_ == scene.volume.nodes.size()) {
      return;
    }
    if (!ensureVolumeDescriptorResources()) {
      return;
    }

    std::vector<glm::vec4> nodeMin;
    std::vector<glm::vec4> nodeMax;
    std::vector<glm::ivec4> childA;
    std::vector<glm::ivec4> childB;
    std::vector<glm::vec4> cornerLo;
    std::vector<glm::vec4> cornerHi;
    nodeMin.reserve(scene.volume.nodes.size());
    nodeMax.reserve(scene.volume.nodes.size());
    childA.reserve(scene.volume.nodes.size());
    childB.reserve(scene.volume.nodes.size());
    cornerLo.reserve(scene.volume.nodes.size());
    cornerHi.reserve(scene.volume.nodes.size());
    for (const AdaptiveVolumeTreeNode& node : scene.volume.nodes) {
      nodeMin.emplace_back(node.boundsMin, node.sigmaAvg);
      nodeMax.emplace_back(node.boundsMax, node.sigmaMax);
      childA.emplace_back(node.child[0],
                          node.child[1],
                          node.child[2],
                          node.child[3]);
      childB.emplace_back(node.child[4],
                          node.child[5],
                          node.child[6],
                          node.child[7]);
      cornerLo.emplace_back(node.cornerSigma[0],
                            node.cornerSigma[1],
                            node.cornerSigma[2],
                            node.cornerSigma[3]);
      cornerHi.emplace_back(node.cornerSigma[4],
                            node.cornerSigma[5],
                            node.cornerSigma[6],
                            node.cornerSigma[7]);
    }

    auto uploadVolumeArray = [&](VulkanBuffer& buffer,
                                 const auto& values,
                                 const char* label) {
      const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(values.size() *
                                  sizeof(typename std::decay_t<decltype(values)>::value_type));
      return uploadToBuffer(buffer,
                            values.data(),
                            bytes,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            label);
    };
    if (!uploadVolumeArray(volumeNodeMinBuffer_,
                           nodeMin,
                           "vkMapMemory(volume nodeMin)") ||
        !uploadVolumeArray(volumeNodeMaxBuffer_,
                           nodeMax,
                           "vkMapMemory(volume nodeMax)") ||
        !uploadVolumeArray(volumeChildABuffer_,
                           childA,
                           "vkMapMemory(volume childA)") ||
        !uploadVolumeArray(volumeChildBBuffer_,
                           childB,
                           "vkMapMemory(volume childB)") ||
        !uploadVolumeArray(volumeCornerLoBuffer_,
                           cornerLo,
                           "vkMapMemory(volume cornerLo)") ||
        !uploadVolumeArray(volumeCornerHiBuffer_,
                           cornerHi,
                           "vkMapMemory(volume cornerHi)")) {
      return;
    }
    volumeNodeCount_ = nodeMin.size();
    volumeRoot_ = scene.volume.root;
    volumeUploadedVersion_ = scene.volumeVersion;
    volumeFrameCache_.valid = false;
    updateVolumeDescriptorSet();
  }

  VolumeUniform buildVolumeUniform(float focalScale = 1.0f,
                                   int debugModeOverride = -1) const
  {
    const RenderRuntimeState& render = frame_.runtime;
    VolumeUniform uniform;
    uniform.invProjection = frame_.matrices.invProj;
    uniform.invView = frame_.matrices.invView;
    uniform.cameraForwardFocal =
      glm::vec4(frame_.matrices.camForward,
                frame_.matrices.focalPx * focalScale);
    uniform.rayParams = glm::vec4(render.volume.pixelThreshold,
                                  render.volume.tauMax,
                                  render.volume.stepBias,
                                  render.volume.skipEpsilon);
    uniform.baseColorAndMode =
      glm::vec4(render.volume.baseColor,
                static_cast<float>(std::clamp(render.volume.colorMode, 0, 2)));
    uniform.tfRangeScale = glm::vec4(render.volume.tfValueMin,
                                     render.volume.tfValueMax,
                                     render.volume.tfSigmaScale,
                                     render.volume.tfMaxSigma);
    uniform.tfControl =
      glm::ivec4(render.volume.tfLogScale ? 1 : 0,
                 std::min(static_cast<int>(render.volume.tfComponents.size()),
                          static_cast<int>(kVolumeTransferSlots)),
                 volumeRoot_,
                   debugModeOverride >= 0
                   ? debugModeOverride
                   : render.volume.debugMode);
    uniform.colorControl =
      glm::ivec4(std::clamp(render.volume.colormapIndex,
                            0,
                            std::max(0, AvailableColormapCount() - 1)),
                 std::max(1, AvailableColormapCount()),
                 0,
                 0);
    uniform.opticalParams =
      glm::vec4(static_cast<float>(std::clamp(render.volume.opticalModel,
                                              0,
                2)),
                std::max(render.volume.emissionScale, 0.0f),
                std::max(render.volume.absorptionScale, 0.0f),
                static_cast<float>(
                  std::clamp(render.volume.maxSamplesPerCell, 1, 256)));

    for (std::size_t i = 0; i < kVolumeTransferSlots; ++i) {
      const std::size_t group = i / 4;
      const std::size_t slot = i & 3u;
      if (i < render.volume.tfComponents.size()) {
        const auto& comp = render.volume.tfComponents[i];
        uniform.tfType[group][slot] = comp.type;
        uniform.tfLogDomain[group][slot] = comp.logDomain ? 1 : 0;
        uniform.tfCenter[group][slot] = comp.center;
        uniform.tfWidth[group][slot] = comp.width;
        uniform.tfAmp[group][slot] = comp.amplitude;
      }
    }
    return uniform;
  }

  bool uploadVolumeUniform(const VolumeUniform& uniform)
  {
    if (!ensureVolumeDescriptorResources() ||
        volumeUniformBuffer_.memory == VK_NULL_HANDLE) {
      return false;
    }
    uploadToBuffer(volumeUniformBuffer_,
                   &uniform,
                   sizeof(uniform),
                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                   "vkMapMemory(volume uniform)");
    return true;
  }

  bool syncVolumeUniform()
  {
    if (!frame_.valid) {
      return false;
    }
    return uploadVolumeUniform(buildVolumeUniform());
  }
#endif

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

  void recordImageLayoutTransition(VkCommandBuffer commandBuffer,
                                   VkImage image,
                                   VkImageLayout oldLayout,
                                   VkImageLayout newLayout,
                                   VkImageAspectFlags aspectMask =
                                     VK_IMAGE_ASPECT_COLOR_BIT)
  {
    if (image == VK_NULL_HANDLE || oldLayout == newLayout) {
      return;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barrier.dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      srcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
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
  }

  VulkanImage createImage(std::uint32_t width,
                          std::uint32_t height,
                          VkFormat format,
                          VkImageUsageFlags usage,
                          VkImageAspectFlags aspectMask =
                            VK_IMAGE_ASPECT_COLOR_BIT)
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
    viewInfo.subresourceRange.aspectMask = aspectMask;
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

  void destroyParticleLodTreeBuffers()
  {
    destroyBuffer(particleLodTreeBuffers_.nodeCenterRadius);
    destroyBuffer(particleLodTreeBuffers_.representativePosHsml);
    destroyBuffer(particleLodTreeBuffers_.representativeValue);
    destroyBuffer(particleLodTreeBuffers_.nodeMeta);
    destroyBuffer(particleLodTreeBuffers_.childA);
    destroyBuffer(particleLodTreeBuffers_.childB);
    destroyBuffer(particleLodTreeBuffers_.representativeMeta);
    destroyBuffer(particleLodTreeBuffers_.indices);
    particleLodTreeBuffers_ = VulkanParticleLodTreeBuffers{};
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
#ifdef ISO_CONTOUR
    destroyBuffer(isoContourMesh_.vertexBuffer);
    destroyBuffer(isoContourMesh_.indexBuffer);
    isoContourMesh_.indexCount = 0;
    destroyBuffer(isoContourInstances_.buffer);
#endif
    destroyBuffer(cubeInstances_.buffer);
    destroyBuffer(ellipsoidInstances_.buffer);
    destroyBuffer(diskInstances_.buffer);
    destroyBuffer(lineVertices_.buffer);
    destroyBuffer(cuboidVertices_.buffer);
    destroyBuffer(polyhedronVertices_.buffer);
    destroyBuffer(velocityVertices_.buffer);
    cubeInstances_ = SolidInstanceSet{};
    ellipsoidInstances_ = SolidInstanceSet{};
    diskInstances_ = SolidInstanceSet{};
#ifdef ISO_CONTOUR
    isoContourInstances_ = SolidInstanceSet{};
#endif
    lineVertices_ = LineVertexSet{};
    cuboidVertices_ = LineVertexSet{};
    polyhedronVertices_ = LineVertexSet{};
    velocityVertices_ = LineVertexSet{};
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

  bool ensureParticlePipelinesForRenderPass(
    VkRenderPass renderPass,
    std::array<VkPipeline, 2>& pipelines,
    VkRenderPass& pipelineRenderPass)
  {
    if (pipelines[0] != VK_NULL_HANDLE && pipelines[1] != VK_NULL_HANDLE &&
        pipelineRenderPass == renderPass) {
      return true;
    }
    for (VkPipeline& pipeline : pipelines) {
      if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
      }
    }
    pipelineRenderPass = VK_NULL_HANDLE;
    if (renderPass == VK_NULL_HANDLE || !ensureDescriptorResources()) {
      return false;
    }
    if (pipelineLayout_ == VK_NULL_HANDLE) {
      VkPipelineLayoutCreateInfo layoutInfo{};
      layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
      layoutInfo.setLayoutCount = 1;
      layoutInfo.pSetLayouts = &descriptorSetLayout_;
      if (!Check(vkCreatePipelineLayout(device_,
                                        &layoutInfo,
                                        nullptr,
                                        &pipelineLayout_),
                 "vkCreatePipelineLayout(particle cache draw)")) {
        return false;
      }
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
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                     static_cast<std::uint32_t>(offsetof(RenderParticle, pos))};
    attributes[1] = {1, 0, VK_FORMAT_R8_UINT,
                     static_cast<std::uint32_t>(offsetof(RenderParticle, type))};
    attributes[2] = {2, 0, VK_FORMAT_R8_UINT,
                     static_cast<std::uint32_t>(offsetof(RenderParticle,
                                                         flag_stress))};
    attributes[3] = {3, 0, VK_FORMAT_R32_SFLOAT,
                     static_cast<std::uint32_t>(offsetof(RenderParticle, hsml))};
    attributes[4] = {4, 0, VK_FORMAT_R32_SFLOAT,
                     static_cast<std::uint32_t>(offsetof(RenderParticle,
                                                         val_show))};

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
    blendAttachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

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
                                              &pipelines[0]),
                    "vkCreateGraphicsPipelines(particle cache particles)");
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    ok = ok &&
         Check(vkCreateGraphicsPipelines(
                 device_,
                 VK_NULL_HANDLE,
                 1,
                 &pipelineInfo,
                 nullptr,
                 &pipelines[1]),
               "vkCreateGraphicsPipelines(particle cache stress)");

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);
    if (ok) {
      pipelineRenderPass = renderPass;
    }
    return ok;
  }

  bool ensureParticleFrameCachePipeline()
  {
    const VkRenderPass renderPass = context_.renderPass();
    if (particleFrameCache_.pipeline != VK_NULL_HANDLE &&
        particleFrameCache_.pipelineRenderPass == renderPass) {
      return true;
    }
    if (particleFrameCache_.pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, particleFrameCache_.pipeline, nullptr);
      particleFrameCache_.pipeline = VK_NULL_HANDLE;
    }
    particleFrameCache_.pipelineRenderPass = VK_NULL_HANDLE;
    if (renderPass == VK_NULL_HANDLE || !ensureParticleFrameCacheDescriptor()) {
      return false;
    }
    if (particleFrameCache_.pipelineLayout == VK_NULL_HANDLE) {
      VkPipelineLayoutCreateInfo layoutInfo{};
      layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
      layoutInfo.setLayoutCount = 1;
      layoutInfo.pSetLayouts = &particleFrameCache_.descriptorSetLayout;
      if (!Check(vkCreatePipelineLayout(device_,
                                        &layoutInfo,
                                        nullptr,
                                        &particleFrameCache_.pipelineLayout),
                 "vkCreatePipelineLayout(particle cache blit)")) {
        return false;
      }
    }

    const std::vector<std::uint32_t> vert =
      ReadSpirv(ShaderPath("volume.vert.spv"));
    const std::vector<std::uint32_t> frag =
      ReadSpirv(ShaderPath("particle_cache.frag.spv"));
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

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
    blendAttachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
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
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

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
    pipelineInfo.layout = particleFrameCache_.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    const bool ok = Check(vkCreateGraphicsPipelines(
                            device_,
                            VK_NULL_HANDLE,
                            1,
                            &pipelineInfo,
                            nullptr,
                            &particleFrameCache_.pipeline),
                          "vkCreateGraphicsPipelines(particle cache blit)");
    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);
    if (ok) {
      particleFrameCache_.pipelineRenderPass = renderPass;
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

#ifdef VOLUME_RENDERING
  void destroyVolumePipeline()
  {
    if (device_ == VK_NULL_HANDLE) {
      return;
    }
    if (volumeFrameCache_.pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, volumeFrameCache_.pipeline, nullptr);
      volumeFrameCache_.pipeline = VK_NULL_HANDLE;
    }
    if (volumeFrameCache_.pipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_,
                              volumeFrameCache_.pipelineLayout,
                              nullptr);
      volumeFrameCache_.pipelineLayout = VK_NULL_HANDLE;
    }
    volumeFrameCache_.pipelineRenderPass = VK_NULL_HANDLE;
    if (volumeCacheRayPipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, volumeCacheRayPipeline_, nullptr);
      volumeCacheRayPipeline_ = VK_NULL_HANDLE;
    }
    volumeCacheRayPipelineRenderPass_ = VK_NULL_HANDLE;
    if (volumeStatsPipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, volumeStatsPipeline_, nullptr);
      volumeStatsPipeline_ = VK_NULL_HANDLE;
    }
    volumeStatsPipelineRenderPass_ = VK_NULL_HANDLE;
    if (volumePipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, volumePipeline_, nullptr);
      volumePipeline_ = VK_NULL_HANDLE;
    }
    if (volumePipelineLayout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, volumePipelineLayout_, nullptr);
      volumePipelineLayout_ = VK_NULL_HANDLE;
    }
    volumePipelineRenderPass_ = VK_NULL_HANDLE;
  }

  void destroyVolumeResources()
  {
    destroyVolumePipeline();
    destroyVolumeFrameCache();
    if (volumeTimingQueryPool_ != VK_NULL_HANDLE) {
      vkDestroyQueryPool(device_, volumeTimingQueryPool_, nullptr);
      volumeTimingQueryPool_ = VK_NULL_HANDLE;
    }
    volumeTimingQueryPending_ = false;
    volumeTimingWallStartValid_ = false;
    destroyBuffer(volumeNodeMinBuffer_);
    destroyBuffer(volumeNodeMaxBuffer_);
    destroyBuffer(volumeChildABuffer_);
    destroyBuffer(volumeChildBBuffer_);
    destroyBuffer(volumeCornerLoBuffer_);
    destroyBuffer(volumeCornerHiBuffer_);
    destroyBuffer(volumeUniformBuffer_);
    volumeNodeCount_ = 0;
    volumeRoot_ = -1;
    volumeUploadedVersion_ = 0;
    volumeDescriptorReady_ = false;
    if (volumeDescriptorPool_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_, volumeDescriptorPool_, nullptr);
      volumeDescriptorPool_ = VK_NULL_HANDLE;
      volumeDescriptorSet_ = VK_NULL_HANDLE;
    }
    if (volumeDescriptorSetLayout_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device_, volumeDescriptorSetLayout_, nullptr);
      volumeDescriptorSetLayout_ = VK_NULL_HANDLE;
    }
  }

  void destroyVolumeFrameCache()
  {
    if (device_ == VK_NULL_HANDLE) {
      return;
    }
    if (volumeFrameCache_.pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, volumeFrameCache_.pipeline, nullptr);
      volumeFrameCache_.pipeline = VK_NULL_HANDLE;
    }
    if (volumeFrameCache_.pipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_,
                              volumeFrameCache_.pipelineLayout,
                              nullptr);
      volumeFrameCache_.pipelineLayout = VK_NULL_HANDLE;
    }
    if (volumeFrameCache_.framebuffer != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(device_, volumeFrameCache_.framebuffer, nullptr);
      volumeFrameCache_.framebuffer = VK_NULL_HANDLE;
    }
    destroyImage(volumeFrameCache_.image);
    if (volumeFrameCache_.sampler != VK_NULL_HANDLE) {
      vkDestroySampler(device_, volumeFrameCache_.sampler, nullptr);
      volumeFrameCache_.sampler = VK_NULL_HANDLE;
    }
    if (volumeFrameCache_.descriptorPool != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_,
                              volumeFrameCache_.descriptorPool,
                              nullptr);
      volumeFrameCache_.descriptorPool = VK_NULL_HANDLE;
      volumeFrameCache_.descriptorSet = VK_NULL_HANDLE;
    }
    if (volumeFrameCache_.descriptorSetLayout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device_,
                                   volumeFrameCache_.descriptorSetLayout,
                                   nullptr);
      volumeFrameCache_.descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (volumeFrameCache_.renderPass != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, volumeFrameCache_.renderPass, nullptr);
      volumeFrameCache_.renderPass = VK_NULL_HANDLE;
    }
    volumeFrameCache_.width = 0;
    volumeFrameCache_.height = 0;
    volumeFrameCache_.pipelineRenderPass = VK_NULL_HANDLE;
    volumeFrameCache_.volumeVersion = 0;
    volumeFrameCache_.uniformInitialized = false;
    volumeFrameCache_.imageInitialized = false;
    volumeFrameCache_.valid = false;
  }

  bool ensureVolumeCacheRenderPass()
  {
    if (volumeFrameCache_.renderPass != VK_NULL_HANDLE) {
      return true;
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies;
    return Check(vkCreateRenderPass(device_,
                                    &renderPassInfo,
                                    nullptr,
                                    &volumeFrameCache_.renderPass),
                 "vkCreateRenderPass(volume cache)");
  }

  bool ensureVolumeFrameCacheSampler()
  {
    if (volumeFrameCache_.sampler != VK_NULL_HANDLE) {
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
    return Check(vkCreateSampler(device_,
                                 &samplerInfo,
                                 nullptr,
                                 &volumeFrameCache_.sampler),
                 "vkCreateSampler(volume cache)");
  }

  bool ensureVolumeFrameCacheDescriptor()
  {
    if (!ensureVolumeFrameCacheSampler()) {
      return false;
    }
    if (volumeFrameCache_.descriptorSetLayout == VK_NULL_HANDLE) {
      VkDescriptorSetLayoutBinding binding{};
      binding.binding = 0;
      binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      binding.descriptorCount = 1;
      binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

      VkDescriptorSetLayoutCreateInfo layoutInfo{};
      layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      layoutInfo.bindingCount = 1;
      layoutInfo.pBindings = &binding;
      if (!Check(vkCreateDescriptorSetLayout(device_,
                                             &layoutInfo,
                                             nullptr,
                                             &volumeFrameCache_.descriptorSetLayout),
                 "vkCreateDescriptorSetLayout(volume cache)")) {
        return false;
      }
    }
    if (volumeFrameCache_.descriptorPool == VK_NULL_HANDLE) {
      VkDescriptorPoolSize poolSize{};
      poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      poolSize.descriptorCount = 1;

      VkDescriptorPoolCreateInfo poolInfo{};
      poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      poolInfo.maxSets = 1;
      poolInfo.poolSizeCount = 1;
      poolInfo.pPoolSizes = &poolSize;
      if (!Check(vkCreateDescriptorPool(device_,
                                        &poolInfo,
                                        nullptr,
                                        &volumeFrameCache_.descriptorPool),
                 "vkCreateDescriptorPool(volume cache)")) {
        return false;
      }
    }
    if (volumeFrameCache_.descriptorSet == VK_NULL_HANDLE) {
      VkDescriptorSetAllocateInfo allocInfo{};
      allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      allocInfo.descriptorPool = volumeFrameCache_.descriptorPool;
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts = &volumeFrameCache_.descriptorSetLayout;
      if (!Check(vkAllocateDescriptorSets(device_,
                                          &allocInfo,
                                          &volumeFrameCache_.descriptorSet),
                 "vkAllocateDescriptorSets(volume cache)")) {
        return false;
      }
    }
    return true;
  }

  void updateVolumeFrameCacheDescriptor()
  {
    if (volumeFrameCache_.descriptorSet == VK_NULL_HANDLE ||
        volumeFrameCache_.image.view == VK_NULL_HANDLE ||
        volumeFrameCache_.sampler == VK_NULL_HANDLE) {
      return;
    }
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = volumeFrameCache_.sampler;
    imageInfo.imageView = volumeFrameCache_.image.view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = volumeFrameCache_.descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
  }

  bool ensureVolumeFrameCacheTarget(std::uint32_t width, std::uint32_t height)
  {
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    if (!ensureVolumeCacheRenderPass() || !ensureVolumeFrameCacheDescriptor()) {
      return false;
    }
    if (volumeFrameCache_.width == width &&
        volumeFrameCache_.height == height &&
        volumeFrameCache_.image.image != VK_NULL_HANDLE &&
        volumeFrameCache_.framebuffer != VK_NULL_HANDLE) {
      return true;
    }

    if (volumeFrameCache_.framebuffer != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(device_, volumeFrameCache_.framebuffer, nullptr);
      volumeFrameCache_.framebuffer = VK_NULL_HANDLE;
    }
    destroyImage(volumeFrameCache_.image);
    volumeFrameCache_.image =
      createImage(width,
                  height,
                  VK_FORMAT_R16G16B16A16_SFLOAT,
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    if (volumeFrameCache_.image.image == VK_NULL_HANDLE) {
      return false;
    }

    VkImageView attachments[] = {volumeFrameCache_.image.view};
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = volumeFrameCache_.renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;
    if (!Check(vkCreateFramebuffer(device_,
                                   &framebufferInfo,
                                   nullptr,
                                   &volumeFrameCache_.framebuffer),
               "vkCreateFramebuffer(volume cache)")) {
      return false;
    }

    volumeFrameCache_.width = width;
    volumeFrameCache_.height = height;
    volumeFrameCache_.imageInitialized = false;
    volumeFrameCache_.valid = false;
    updateVolumeFrameCacheDescriptor();
    return true;
  }

  bool ensureVolumePipelineLayout()
  {
    if (volumePipelineLayout_ != VK_NULL_HANDLE) {
      return true;
    }
    if (!ensureVolumeDescriptorResources()) {
      return false;
    }

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &volumeDescriptorSetLayout_;
    if (!Check(vkCreatePipelineLayout(device_,
                                      &layoutInfo,
                                      nullptr,
                                      &volumePipelineLayout_),
               "vkCreatePipelineLayout(volume)")) {
      return false;
    }
    return true;
  }

  bool ensureVolumePipeline()
  {
    const VkRenderPass renderPass = context_.renderPass();
    return ensureVolumeRayPipeline(renderPass,
                                   volumePipeline_,
                                   volumePipelineRenderPass_);
  }

  bool ensureVolumeRayPipeline(VkRenderPass renderPass,
                               VkPipeline& pipeline,
                               VkRenderPass& pipelineRenderPass,
                               bool enableBlend = true)
  {
    if (volumePipeline_ != VK_NULL_HANDLE &&
        &pipeline == &volumePipeline_ &&
        pipelineRenderPass == renderPass) {
      return true;
    }
    if (pipeline != VK_NULL_HANDLE && pipelineRenderPass == renderPass) {
      return true;
    }
    if (pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, pipeline, nullptr);
      pipeline = VK_NULL_HANDLE;
    }
    pipelineRenderPass = VK_NULL_HANDLE;
    if (renderPass == VK_NULL_HANDLE || !ensureVolumePipelineLayout()) {
      return false;
    }

    const std::vector<std::uint32_t> vert =
      ReadSpirv(ShaderPath("volume.vert.spv"));
    const std::vector<std::uint32_t> frag =
      ReadSpirv(ShaderPath("volume.frag.spv"));
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

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
    blendAttachment.blendEnable = enableBlend ? VK_TRUE : VK_FALSE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
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
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

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
    pipelineInfo.layout = volumePipelineLayout_;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    const bool ok = Check(vkCreateGraphicsPipelines(device_,
                                                    VK_NULL_HANDLE,
                                                    1,
                                                    &pipelineInfo,
                                                    nullptr,
                                                    &pipeline),
                          "vkCreateGraphicsPipelines(volume)");
    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);
    if (ok) {
      pipelineRenderPass = renderPass;
    }
    return ok;
  }

  bool ensureVolumeCachePipeline()
  {
    const VkRenderPass renderPass = context_.renderPass();
    if (volumeFrameCache_.pipeline != VK_NULL_HANDLE &&
        volumeFrameCache_.pipelineRenderPass == renderPass) {
      return true;
    }
    if (volumeFrameCache_.pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, volumeFrameCache_.pipeline, nullptr);
      volumeFrameCache_.pipeline = VK_NULL_HANDLE;
    }
    volumeFrameCache_.pipelineRenderPass = VK_NULL_HANDLE;
    if (renderPass == VK_NULL_HANDLE || !ensureVolumeFrameCacheDescriptor()) {
      return false;
    }
    if (volumeFrameCache_.pipelineLayout == VK_NULL_HANDLE) {
      VkPipelineLayoutCreateInfo layoutInfo{};
      layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
      layoutInfo.setLayoutCount = 1;
      layoutInfo.pSetLayouts = &volumeFrameCache_.descriptorSetLayout;
      if (!Check(vkCreatePipelineLayout(device_,
                                        &layoutInfo,
                                        nullptr,
                                        &volumeFrameCache_.pipelineLayout),
                 "vkCreatePipelineLayout(volume cache)")) {
        return false;
      }
    }

    const std::vector<std::uint32_t> vert =
      ReadSpirv(ShaderPath("volume.vert.spv"));
    const std::vector<std::uint32_t> frag =
      ReadSpirv(ShaderPath("volume_cache.frag.spv"));
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

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
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
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

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
    pipelineInfo.layout = volumeFrameCache_.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    const bool ok = Check(vkCreateGraphicsPipelines(device_,
                                                    VK_NULL_HANDLE,
                                                    1,
                                                    &pipelineInfo,
                                                    nullptr,
                                                    &volumeFrameCache_.pipeline),
                          "vkCreateGraphicsPipelines(volume cache)");
    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);
    if (ok) {
      volumeFrameCache_.pipelineRenderPass = renderPass;
    }
    return ok;
  }
#endif

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
#ifdef VOLUME_RENDERING
    destroyVolumePipeline();
#endif
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

  void destroyParticleFrameCache()
  {
    if (device_ == VK_NULL_HANDLE) {
      return;
    }
    if (particleFrameCache_.pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, particleFrameCache_.pipeline, nullptr);
      particleFrameCache_.pipeline = VK_NULL_HANDLE;
    }
    if (particleFrameCache_.pipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_,
                              particleFrameCache_.pipelineLayout,
                              nullptr);
      particleFrameCache_.pipelineLayout = VK_NULL_HANDLE;
    }
    for (VkPipeline& pipeline : particleFrameCache_.particlePipelines) {
      if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
      }
    }
    particleFrameCache_.pipelineRenderPass = VK_NULL_HANDLE;
    particleFrameCache_.particlePipelineRenderPass = VK_NULL_HANDLE;
    if (particleFrameCache_.framebuffer != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(device_, particleFrameCache_.framebuffer, nullptr);
      particleFrameCache_.framebuffer = VK_NULL_HANDLE;
    }
    destroyImage(particleFrameCache_.color);
    destroyImage(particleFrameCache_.depth);
    if (particleFrameCache_.sampler != VK_NULL_HANDLE) {
      vkDestroySampler(device_, particleFrameCache_.sampler, nullptr);
      particleFrameCache_.sampler = VK_NULL_HANDLE;
    }
    if (particleFrameCache_.descriptorPool != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_,
                              particleFrameCache_.descriptorPool,
                              nullptr);
      particleFrameCache_.descriptorPool = VK_NULL_HANDLE;
      particleFrameCache_.descriptorSet = VK_NULL_HANDLE;
    }
    if (particleFrameCache_.descriptorSetLayout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device_,
                                   particleFrameCache_.descriptorSetLayout,
                                   nullptr);
      particleFrameCache_.descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (particleFrameCache_.renderPass != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, particleFrameCache_.renderPass, nullptr);
      particleFrameCache_.renderPass = VK_NULL_HANDLE;
    }
    particleFrameCache_.width = 0;
    particleFrameCache_.height = 0;
    particleFrameCache_.particlesVersion = 0;
    particleFrameCache_.imageInitialized = false;
    particleFrameCache_.valid = false;
  }

  bool ensureParticleFrameCacheRenderPass()
  {
    if (particleFrameCache_.renderPass != VK_NULL_HANDLE) {
      return true;
    }

    std::array<VkAttachmentDescription, 2> attachments{};
    attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask =
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount =
      static_cast<std::uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount =
      static_cast<std::uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();
    return Check(vkCreateRenderPass(device_,
                                    &renderPassInfo,
                                    nullptr,
                                    &particleFrameCache_.renderPass),
                 "vkCreateRenderPass(particle cache)");
  }

  bool ensureParticleFrameCacheSampler()
  {
    if (particleFrameCache_.sampler != VK_NULL_HANDLE) {
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
    return Check(vkCreateSampler(device_,
                                 &samplerInfo,
                                 nullptr,
                                 &particleFrameCache_.sampler),
                 "vkCreateSampler(particle cache)");
  }

  bool ensureParticleFrameCacheDescriptor()
  {
    if (!ensureParticleFrameCacheSampler()) {
      return false;
    }
    if (particleFrameCache_.descriptorSetLayout == VK_NULL_HANDLE) {
      std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
      for (std::uint32_t i = 0; i < 2; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType =
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
      }

      VkDescriptorSetLayoutCreateInfo layoutInfo{};
      layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      layoutInfo.bindingCount =
        static_cast<std::uint32_t>(bindings.size());
      layoutInfo.pBindings = bindings.data();
      if (!Check(vkCreateDescriptorSetLayout(device_,
                                             &layoutInfo,
                                             nullptr,
                                             &particleFrameCache_.descriptorSetLayout),
                 "vkCreateDescriptorSetLayout(particle cache)")) {
        return false;
      }
    }
    if (particleFrameCache_.descriptorPool == VK_NULL_HANDLE) {
      VkDescriptorPoolSize poolSize{};
      poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      poolSize.descriptorCount = 2;

      VkDescriptorPoolCreateInfo poolInfo{};
      poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      poolInfo.maxSets = 1;
      poolInfo.poolSizeCount = 1;
      poolInfo.pPoolSizes = &poolSize;
      if (!Check(vkCreateDescriptorPool(device_,
                                        &poolInfo,
                                        nullptr,
                                        &particleFrameCache_.descriptorPool),
                 "vkCreateDescriptorPool(particle cache)")) {
        return false;
      }
    }
    if (particleFrameCache_.descriptorSet == VK_NULL_HANDLE) {
      VkDescriptorSetAllocateInfo allocInfo{};
      allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      allocInfo.descriptorPool = particleFrameCache_.descriptorPool;
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts = &particleFrameCache_.descriptorSetLayout;
      if (!Check(vkAllocateDescriptorSets(device_,
                                          &allocInfo,
                                          &particleFrameCache_.descriptorSet),
                 "vkAllocateDescriptorSets(particle cache)")) {
        return false;
      }
    }
    return true;
  }

  void updateParticleFrameCacheDescriptor()
  {
    if (particleFrameCache_.descriptorSet == VK_NULL_HANDLE ||
        particleFrameCache_.color.view == VK_NULL_HANDLE ||
        particleFrameCache_.depth.view == VK_NULL_HANDLE ||
        particleFrameCache_.sampler == VK_NULL_HANDLE) {
      return;
    }

    std::array<VkDescriptorImageInfo, 2> imageInfos{};
    imageInfos[0].sampler = particleFrameCache_.sampler;
    imageInfos[0].imageView = particleFrameCache_.color.view;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].sampler = particleFrameCache_.sampler;
    imageInfos[1].imageView = particleFrameCache_.depth.view;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 2> writes{};
    for (std::uint32_t i = 0; i < 2; ++i) {
      writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet = particleFrameCache_.descriptorSet;
      writes[i].dstBinding = i;
      writes[i].descriptorCount = 1;
      writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writes[i].pImageInfo = &imageInfos[i];
    }
    vkUpdateDescriptorSets(device_,
                           static_cast<std::uint32_t>(writes.size()),
                           writes.data(),
                           0,
                           nullptr);
  }

  bool ensureParticleFrameCacheTarget(std::uint32_t width,
                                      std::uint32_t height)
  {
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    if (!ensureParticleFrameCacheRenderPass() ||
        !ensureParticleFrameCacheDescriptor()) {
      return false;
    }
    if (particleFrameCache_.width == width &&
        particleFrameCache_.height == height &&
        particleFrameCache_.color.image != VK_NULL_HANDLE &&
        particleFrameCache_.depth.image != VK_NULL_HANDLE &&
        particleFrameCache_.framebuffer != VK_NULL_HANDLE) {
      return true;
    }

    if (particleFrameCache_.framebuffer != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(device_, particleFrameCache_.framebuffer, nullptr);
      particleFrameCache_.framebuffer = VK_NULL_HANDLE;
    }
    destroyImage(particleFrameCache_.color);
    destroyImage(particleFrameCache_.depth);

    particleFrameCache_.color =
      createImage(width,
                  height,
                  VK_FORMAT_R16G16B16A16_SFLOAT,
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT);
    particleFrameCache_.depth =
      createImage(width,
                  height,
                  VK_FORMAT_D32_SFLOAT,
                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                  VK_IMAGE_ASPECT_DEPTH_BIT);
    if (particleFrameCache_.color.image == VK_NULL_HANDLE ||
        particleFrameCache_.depth.image == VK_NULL_HANDLE) {
      return false;
    }

    std::array<VkImageView, 2> attachments = {
      particleFrameCache_.color.view,
      particleFrameCache_.depth.view
    };
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = particleFrameCache_.renderPass;
    framebufferInfo.attachmentCount =
      static_cast<std::uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;
    if (!Check(vkCreateFramebuffer(device_,
                                   &framebufferInfo,
                                   nullptr,
                                   &particleFrameCache_.framebuffer),
               "vkCreateFramebuffer(particle cache)")) {
      return false;
    }

    particleFrameCache_.width = width;
    particleFrameCache_.height = height;
    particleFrameCache_.imageInitialized = false;
    particleFrameCache_.valid = false;
    updateParticleFrameCacheDescriptor();
    return true;
  }

  bool particleFrameCacheMatches(std::uint32_t width,
                                 std::uint32_t height) const
  {
    return particleFrameCache_.valid &&
           particleFrameCache_.particlesVersion == particleVersion_ &&
           particleFrameCache_.width == width &&
           particleFrameCache_.height == height &&
           EqualMatrix(particleFrameCache_.model, frame_.matrices.model) &&
           EqualMatrix(particleFrameCache_.view, frame_.matrices.view) &&
           EqualMatrix(particleFrameCache_.projection,
                       frame_.matrices.projection) &&
           EqualParticleVisualConfig(particleFrameCache_.visualConfig,
                                     frame_.particleVisual);
  }

  void recordViewportAndScissor(VkCommandBuffer commandBuffer,
                                std::uint32_t width,
                                std::uint32_t height) const
  {
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width, height};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
  }

  void updateParticleFrameCache(VkCommandBuffer commandBuffer)
  {
    if (!frame_.valid ||
        useParticleLod_ ||
        !frame_.runtime.scheduling.cacheParticleFrames ||
        frame_.runtime.scheduling.interactionActive ||
        particleCount_ == 0) {
      particleFrameCache_.valid = false;
      return;
    }

    const VkExtent2D extent = context_.swapchainExtent();
    if (extent.width == 0 || extent.height == 0) {
      return;
    }
    if (particleFrameCacheMatches(extent.width, extent.height)) {
      return;
    }
    if (!ensureParticleFrameCacheTarget(extent.width, extent.height) ||
        !ensureParticlePipelinesForRenderPass(
          particleFrameCache_.renderPass,
          particleFrameCache_.particlePipelines,
          particleFrameCache_.particlePipelineRenderPass)) {
      particleFrameCache_.valid = false;
      return;
    }

    recordImageLayoutTransition(
      commandBuffer,
      particleFrameCache_.color.image,
      particleFrameCache_.imageInitialized
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    recordImageLayoutTransition(
      commandBuffer,
      particleFrameCache_.depth.image,
      particleFrameCache_.imageInitialized
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      VK_IMAGE_ASPECT_DEPTH_BIT);
    particleFrameCache_.imageInitialized = true;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color.float32[0] = 0.0f;
    clearValues[0].color.float32[1] = 0.0f;
    clearValues[0].color.float32[2] = 0.0f;
    clearValues[0].color.float32[3] = 0.0f;
    clearValues[1].depthStencil.depth = 1.0f;

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = particleFrameCache_.renderPass;
    renderPassInfo.framebuffer = particleFrameCache_.framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {extent.width, extent.height};
    renderPassInfo.clearValueCount =
      static_cast<std::uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer,
                         &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    recordViewportAndScissor(commandBuffer, extent.width, extent.height);
    drawParticleBuffer(commandBuffer,
                       particleBuffer_,
                       particleCount_,
                       0,
                       particleFrameCache_.particlePipelines[0]);
    vkCmdEndRenderPass(commandBuffer);

    recordImageLayoutTransition(commandBuffer,
                                particleFrameCache_.color.image,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    recordImageLayoutTransition(
      commandBuffer,
      particleFrameCache_.depth.image,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_DEPTH_BIT);

    particleFrameCache_.particlesVersion = particleVersion_;
    particleFrameCache_.model = frame_.matrices.model;
    particleFrameCache_.view = frame_.matrices.view;
    particleFrameCache_.projection = frame_.matrices.projection;
    particleFrameCache_.visualConfig = frame_.particleVisual;
    particleFrameCache_.valid = true;
  }

  bool drawParticleFrameCache(VkCommandBuffer commandBuffer)
  {
    if (!particleFrameCache_.valid ||
        particleFrameCache_.descriptorSet == VK_NULL_HANDLE ||
        !ensureParticleFrameCachePipeline()) {
      return false;
    }
    vkCmdBindPipeline(commandBuffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      particleFrameCache_.pipeline);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            particleFrameCache_.pipelineLayout,
                            0,
                            1,
                            &particleFrameCache_.descriptorSet,
                            0,
                            nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    return true;
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

#ifdef VOLUME_RENDERING
    if (frame_.runtime.scheduling.cacheVolumeFrames) {
      if (!drawCachedVolumeFrame(commandBuffer)) {
        drawVolume(commandBuffer);
      }
    } else {
      drawVolume(commandBuffer);
    }
#endif
    if (useParticleLod_) {
      drawParticleBuffer(commandBuffer,
                         particleLodBuffer_,
                         particleLodCount_,
                         0);
      drawParticleBuffer(commandBuffer,
                         stressParticleLodBuffer_,
                         stressParticleLodCount_,
                         1);
    } else if (frame_.runtime.scheduling.cacheParticleFrames &&
               !frame_.runtime.scheduling.interactionActive &&
               drawParticleFrameCache(commandBuffer)) {
      drawParticleBuffer(commandBuffer,
                         stressParticleBuffer_,
                         stressParticleCount_,
                         1);
    } else {
      particleFrameCache_.valid = false;
      drawParticleBuffer(commandBuffer, particleBuffer_, particleCount_, 0);
      drawParticleBuffer(commandBuffer,
                         stressParticleBuffer_,
                         stressParticleCount_,
                         1);
    }
    drawLineSet(commandBuffer, velocityVertices_, frame_.runtime.velocity);
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
#ifdef ISO_CONTOUR
    drawSolidLayer(commandBuffer,
                   isoContourMesh_,
                   isoContourInstances_,
                   frame_.runtime.isocontour);
#endif
    drawLineSet(commandBuffer, cuboidVertices_, frame_.runtime.cuboids);
    drawLineSet(commandBuffer, lineVertices_, frame_.runtime.lines);
    drawLineSet(commandBuffer, polyhedronVertices_, frame_.runtime.polyhedra);
  }

#ifdef VOLUME_RENDERING
  bool shouldSkipVolumeForInteraction() const
  {
    const RenderRuntimeState& render = frame_.runtime;
    return render.scheduling.responsiveInteraction &&
           render.scheduling.interactionActive &&
           render.scheduling.skipVolumeWhileInteracting;
  }

  bool ensureVolumeTimingQueryPool()
  {
    if (volumeTimingQueryPool_ != VK_NULL_HANDLE) {
      return true;
    }
    if (timestampPeriodNs_ <= 0.0f) {
      return false;
    }
    VkQueryPoolCreateInfo queryInfo{};
    queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = 2;
    return Check(vkCreateQueryPool(device_,
                                   &queryInfo,
                                   nullptr,
                                   &volumeTimingQueryPool_),
                 "vkCreateQueryPool(volume timing)");
  }

  void collectVolumeTimingQueryResult()
  {
    if (!volumeTimingQueryPending_ ||
        volumeTimingQueryPool_ == VK_NULL_HANDLE ||
        timestampPeriodNs_ <= 0.0f) {
      return;
    }

    std::uint64_t ticks[2] = {0, 0};
    const VkResult result =
      vkGetQueryPoolResults(device_,
                            volumeTimingQueryPool_,
                            0,
                            2,
                            sizeof(ticks),
                            ticks,
                            sizeof(std::uint64_t),
                            VK_QUERY_RESULT_64_BIT);
    if (result == VK_NOT_READY) {
      return;
    }
    volumeTimingQueryPending_ = false;
    if (result != VK_SUCCESS || ticks[1] <= ticks[0]) {
      return;
    }
    timing_.volumeGpuTimeKnown = true;
    timing_.volumeGpuMs =
      static_cast<double>(ticks[1] - ticks[0]) *
      static_cast<double>(timestampPeriodNs_) * 1.0e-6;
    if (volumeTimingWallStartValid_) {
      const auto now = std::chrono::steady_clock::now();
      timing_.volumeWallLatencyKnown = true;
      timing_.volumeWallLatencyMs =
        std::chrono::duration<double, std::milli>(
          now - volumeTimingWallStart_).count();
      volumeTimingWallStartValid_ = false;
    }
  }

  bool beginVolumeTiming(VkCommandBuffer commandBuffer)
  {
    if (!ensureVolumeTimingQueryPool()) {
      return false;
    }
    vkCmdResetQueryPool(commandBuffer, volumeTimingQueryPool_, 0, 2);
    vkCmdWriteTimestamp(commandBuffer,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        volumeTimingQueryPool_,
                        0);
    return true;
  }

  void endVolumeTiming(VkCommandBuffer commandBuffer, bool active)
  {
    if (!active || volumeTimingQueryPool_ == VK_NULL_HANDLE) {
      return;
    }
    vkCmdWriteTimestamp(commandBuffer,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        volumeTimingQueryPool_,
                        1);
    volumeTimingQueryPending_ = true;
  }

  bool volumeCanDraw() const
  {
    return frame_.valid &&
           frame_.runtime.volume.show &&
           volumeNodeCount_ > 0 &&
           volumeRoot_ >= 0 &&
           volumeDescriptorReady_;
  }

  bool volumeFrameCacheMatches(const VolumeUniform& uniform,
                               std::uint32_t width,
                               std::uint32_t height) const
  {
    return volumeFrameCache_.valid &&
           volumeFrameCache_.volumeVersion == volumeUploadedVersion_ &&
           volumeFrameCache_.width == width &&
           volumeFrameCache_.height == height &&
           volumeFrameCache_.uniformInitialized &&
           std::memcmp(&volumeFrameCache_.uniform,
                       &uniform,
                       sizeof(VolumeUniform)) == 0;
  }

  void recordVolumeViewportAndScissor(VkCommandBuffer commandBuffer,
                                      std::uint32_t width,
                                      std::uint32_t height) const
  {
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width, height};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
  }

  void updateVolumeFrameCache(VkCommandBuffer commandBuffer)
  {
    collectVolumeTimingQueryResult();
    timing_.volumeCacheUsed = frame_.runtime.scheduling.cacheVolumeFrames;
    timing_.volumeCacheUpdated = false;
    timing_.volumeCacheHit = false;
    timing_.volumeCacheScale =
      std::clamp(static_cast<double>(
                   frame_.runtime.scheduling.volumeFrameCacheScale),
                 0.25,
                 1.0);
    if (!frame_.runtime.scheduling.cacheVolumeFrames ||
        !volumeCanDraw() || shouldSkipVolumeForInteraction()) {
      return;
    }

    const VkExtent2D extent = context_.swapchainExtent();
    if (extent.width == 0 || extent.height == 0) {
      return;
    }
    const float cacheScale =
      std::clamp(frame_.runtime.scheduling.volumeFrameCacheScale, 0.25f, 1.0f);
    const std::uint32_t cacheWidth = std::max(
      1u,
      static_cast<std::uint32_t>(
        std::ceil(static_cast<float>(extent.width) * cacheScale)));
    const std::uint32_t cacheHeight = std::max(
      1u,
      static_cast<std::uint32_t>(
        std::ceil(static_cast<float>(extent.height) * cacheScale)));
    const VolumeUniform uniform = buildVolumeUniform(cacheScale);
    if (volumeFrameCacheMatches(uniform, cacheWidth, cacheHeight)) {
      timing_.volumeCacheHit = true;
      return;
    }
    if (!ensureVolumeFrameCacheTarget(cacheWidth, cacheHeight) ||
        !ensureVolumeRayPipeline(volumeFrameCache_.renderPass,
                                 volumeCacheRayPipeline_,
                                 volumeCacheRayPipelineRenderPass_) ||
        !uploadVolumeUniform(uniform)) {
      return;
    }
    timing_.volumeCacheUpdated = true;

    recordImageLayoutTransition(
      commandBuffer,
      volumeFrameCache_.image.image,
      volumeFrameCache_.imageInitialized
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    volumeFrameCache_.imageInitialized = true;

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = volumeFrameCache_.renderPass;
    renderPassInfo.framebuffer = volumeFrameCache_.framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {cacheWidth, cacheHeight};
    VkClearValue clearValue{};
    clearValue.color.float32[0] = 0.0f;
    clearValue.color.float32[1] = 0.0f;
    clearValue.color.float32[2] = 0.0f;
    clearValue.color.float32[3] = 0.0f;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;
    const bool timingActive = beginVolumeTiming(commandBuffer);
    vkCmdBeginRenderPass(commandBuffer,
                         &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    recordVolumeViewportAndScissor(commandBuffer, cacheWidth, cacheHeight);
    vkCmdBindPipeline(commandBuffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      volumeCacheRayPipeline_);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            volumePipelineLayout_,
                            0,
                            1,
                            &volumeDescriptorSet_,
                            0,
                            nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
    endVolumeTiming(commandBuffer, timingActive);

    recordImageLayoutTransition(commandBuffer,
                                volumeFrameCache_.image.image,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    volumeFrameCache_.volumeVersion = volumeUploadedVersion_;
    volumeFrameCache_.uniform = uniform;
    volumeFrameCache_.uniformInitialized = true;
    volumeFrameCache_.valid = true;
    volumeTimingWallStart_ = std::chrono::steady_clock::now();
    volumeTimingWallStartValid_ = timingActive;
  }

  bool drawCachedVolumeFrame(VkCommandBuffer commandBuffer)
  {
    if (!volumeCanDraw() || shouldSkipVolumeForInteraction() ||
        !volumeFrameCache_.valid ||
        volumeFrameCache_.descriptorSet == VK_NULL_HANDLE ||
        !ensureVolumeCachePipeline()) {
      return false;
    }
    vkCmdBindPipeline(commandBuffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      volumeFrameCache_.pipeline);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            volumeFrameCache_.pipelineLayout,
                            0,
                            1,
                            &volumeFrameCache_.descriptorSet,
                            0,
                            nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    return true;
  }

  void drawVolume(VkCommandBuffer commandBuffer)
  {
    if (!volumeCanDraw() || shouldSkipVolumeForInteraction() ||
        !ensureVolumePipeline()) {
      return;
    }

    if (!syncVolumeUniform()) {
      return;
    }
    if (volumeUniformBuffer_.buffer == VK_NULL_HANDLE ||
        volumeDescriptorSet_ == VK_NULL_HANDLE) {
      return;
    }

    vkCmdBindPipeline(commandBuffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      volumePipeline_);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            volumePipelineLayout_,
                            0,
                            1,
                            &volumeDescriptorSet_,
                            0,
                            nullptr);
    const bool timingActive = beginVolumeTiming(commandBuffer);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    endVolumeTiming(commandBuffer, timingActive);
    volumeTimingWallStart_ = std::chrono::steady_clock::now();
    volumeTimingWallStartValid_ = timingActive;
  }
#endif

  void drawParticleBuffer(VkCommandBuffer commandBuffer,
                          const VulkanBuffer& buffer,
                          std::size_t count,
                          std::size_t passIndex)
  {
    drawParticleBuffer(commandBuffer,
                       buffer,
                       count,
                       passIndex,
                       pipelines_[passIndex]);
  }

  void drawParticleBuffer(VkCommandBuffer commandBuffer,
                          const VulkanBuffer& buffer,
                          std::size_t count,
                          std::size_t passIndex,
                          VkPipeline pipeline)
  {
    if (count == 0 || buffer.buffer == VK_NULL_HANDLE ||
        passIndex >= descriptorSets_.size() ||
        pipeline == VK_NULL_HANDLE ||
        descriptorSets_[passIndex] == VK_NULL_HANDLE) {
      return;
    }

    VkDeviceSize offsets[] = { 0 };
    vkCmdBindPipeline(commandBuffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline);
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
  VulkanBuffer particleLodBuffer_;
  VulkanBuffer stressParticleLodBuffer_;
  VulkanParticleLodTreeBuffers particleLodTreeBuffers_;
  VulkanParticleFrameCache particleFrameCache_;
  std::array<VulkanBuffer, 2> visualBuffers_;
  VulkanImage colormapAtlas_;
  VkSampler colormapSampler_ = VK_NULL_HANDLE;
  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  std::size_t particleCount_ = 0;
  std::size_t stressParticleCount_ = 0;
  std::size_t particleLodCount_ = 0;
  std::size_t stressParticleLodCount_ = 0;
  RenderSceneVersion particleVersion_ = 0;
  RenderSceneVersion stressParticleVersion_ = 0;
  RenderSceneVersion particleLodVersion_ = 0;
  RenderSceneVersion stressParticleLodVersion_ = 0;
  RenderFrameState frame_;
  std::array<float, 6> pointSizes_ = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  std::array<float, 6> valueMin_ = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  std::array<float, 6> valueMax_ = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  std::array<std::uint32_t, 6> colormap_ = {0u, 0u, 0u, 0u, 0u, 0u};
  std::uint32_t hiddenTypeMask_ = 0;
  std::uint32_t logTypeMask_ = 0;
  std::uint32_t periodicTypeMask_ = 0;
  bool visualDirty_ = true;
  bool softwareRenderer_ = false;
  bool useParticleLod_ = false;
  bool memoryBudgetSupported_ = false;
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
#ifdef ISO_CONTOUR
  SolidMesh isoContourMesh_;
#endif
  SolidInstanceSet cubeInstances_;
  SolidInstanceSet ellipsoidInstances_;
  SolidInstanceSet diskInstances_;
#ifdef ISO_CONTOUR
  SolidInstanceSet isoContourInstances_;
  RenderSceneVersion isoContourVersion_ = 0;
#endif
  LineVertexSet lineVertices_;
  LineVertexSet cuboidVertices_;
  LineVertexSet polyhedronVertices_;
  LineVertexSet velocityVertices_;
  RenderSceneVersion velocityVersion_ = 0;
  float velocityArrowScale_ = -1.0f;
  bool velocityUseLogScale_ = false;
  VulkanBuffer solidUniformBuffer_;
  VkDescriptorSetLayout solidDescriptorSetLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool solidDescriptorPool_ = VK_NULL_HANDLE;
  VkDescriptorSet solidDescriptorSet_ = VK_NULL_HANDLE;
  VkPipelineLayout solidPipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline solidPipeline_ = VK_NULL_HANDLE;
  VkRenderPass solidPipelineRenderPass_ = VK_NULL_HANDLE;
  VkPipeline linePipeline_ = VK_NULL_HANDLE;
  VkRenderPass linePipelineRenderPass_ = VK_NULL_HANDLE;
#ifdef VOLUME_RENDERING
  VulkanBuffer volumeNodeMinBuffer_;
  VulkanBuffer volumeNodeMaxBuffer_;
  VulkanBuffer volumeChildABuffer_;
  VulkanBuffer volumeChildBBuffer_;
  VulkanBuffer volumeCornerLoBuffer_;
  VulkanBuffer volumeCornerHiBuffer_;
  VulkanBuffer volumeUniformBuffer_;
  std::size_t volumeNodeCount_ = 0;
  int volumeRoot_ = -1;
  RenderSceneVersion volumeUploadedVersion_ = 0;
  bool volumeDescriptorReady_ = false;
  VkDescriptorSetLayout volumeDescriptorSetLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool volumeDescriptorPool_ = VK_NULL_HANDLE;
  VkDescriptorSet volumeDescriptorSet_ = VK_NULL_HANDLE;
  VkPipelineLayout volumePipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline volumePipeline_ = VK_NULL_HANDLE;
  VkRenderPass volumePipelineRenderPass_ = VK_NULL_HANDLE;
  VkPipeline volumeCacheRayPipeline_ = VK_NULL_HANDLE;
  VkRenderPass volumeCacheRayPipelineRenderPass_ = VK_NULL_HANDLE;
  VkPipeline volumeStatsPipeline_ = VK_NULL_HANDLE;
  VkRenderPass volumeStatsPipelineRenderPass_ = VK_NULL_HANDLE;
  VulkanVolumeFrameCache volumeFrameCache_;
  VkQueryPool volumeTimingQueryPool_ = VK_NULL_HANDLE;
  bool volumeTimingQueryPending_ = false;
  std::chrono::steady_clock::time_point volumeTimingWallStart_;
  bool volumeTimingWallStartValid_ = false;
  float timestampPeriodNs_ = 0.0f;
#endif
  RenderBackendTimingInfo timing_;
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
