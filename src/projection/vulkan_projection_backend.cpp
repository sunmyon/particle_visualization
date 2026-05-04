#include "projection/vulkan_projection_backend.h"

#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
#include "platform/vulkan_context.h"
#include <vulkan/vulkan.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <dlfcn.h>
#endif

namespace {

#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND

struct VulkanProjectionParticle {
  float posVal[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float densityMassHsmlPad[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct VulkanVoronoiUniforms {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t depth = 0;
  std::uint32_t particleCount = 0;
  float dx = 0.0f;
  float dy = 0.0f;
  float dz = 0.0f;
  float xminX = 0.0f;
  float xminY = 0.0f;
  float xminZ = 0.0f;
  std::uint32_t densityWeight = 0;
  std::uint32_t pad0 = 0;
  float renderColor[4] = {0.35f, 1.0f, 0.8f, 0.0f};
  std::uint32_t tfCount = 0;
  std::uint32_t colorMapSize = 0;
  std::uint32_t colorLogScale = 0;
  std::uint32_t pad1 = 0;
  float colorValueMin = 0.0f;
  float colorValueMax = 1.0f;
  float pad2[2] = {0.0f, 0.0f};
};

struct VulkanProjectionTfComponent {
  std::uint32_t type = 0;
  float center = 1.0f;
  float width = 1.0f;
  float amplitude = 0.0f;
  std::uint32_t logDomain = 1;
  float pad[3] = {0.0f, 0.0f, 0.0f};
};

struct VulkanProjectionUniforms {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t densityWeight = 0;
  std::uint32_t particleCount = 0;
  float dx = 0.0f;
  float dy = 0.0f;
  float xminX = 0.0f;
  float xminY = 0.0f;
  float center[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float uAxis[4] = {1.0f, 0.0f, 0.0f, 0.0f};
  float vAxis[4] = {0.0f, 1.0f, 0.0f, 0.0f};
  float valueMin = 0.0f;
  float valueMax = 1.0f;
  float pad0 = 0.0f;
  float pad1 = 0.0f;
};

struct VulkanStepUniform {
  std::uint32_t stepSize = 1;
  std::uint32_t pad[3] = {0, 0, 0};
};

struct VulkanBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
};

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

bool IsExtensionAvailable(const std::vector<VkExtensionProperties>& properties,
                          const char* extension)
{
  return std::any_of(properties.begin(),
                     properties.end(),
                     [&](const VkExtensionProperties& property) {
                       return std::strcmp(property.extensionName,
                                          extension) == 0;
                     });
}

void AppendExtensionIfAvailable(std::vector<const char*>& extensions,
                                const std::vector<VkExtensionProperties>& available,
                                const char* extension)
{
  if (!IsExtensionAvailable(available, extension)) {
    return;
  }
  const bool exists =
    std::any_of(extensions.begin(),
                extensions.end(),
                [&](const char* current) {
                  return std::strcmp(current, extension) == 0;
                });
  if (!exists) {
    extensions.push_back(extension);
  }
}

[[maybe_unused]] bool HasDeviceExtension(VkPhysicalDevice device,
                                         const char* extension)
{
  std::uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> properties(count);
  vkEnumerateDeviceExtensionProperties(device,
                                       nullptr,
                                       &count,
                                       properties.data());
  return IsExtensionAvailable(properties, extension);
}

std::vector<VkExtensionProperties> EnumerateInstanceExtensions()
{
  std::uint32_t count = 0;
  if (!Check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
             "vkEnumerateInstanceExtensionProperties")) {
    return {};
  }
  std::vector<VkExtensionProperties> properties(count);
  Check(vkEnumerateInstanceExtensionProperties(nullptr,
                                               &count,
                                               properties.data()),
        "vkEnumerateInstanceExtensionProperties");
  return properties;
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

std::vector<std::uint32_t> ReadSpirv(const std::string& path)
{
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    std::cerr << "Failed to open Vulkan projection shader: " << path
              << std::endl;
    return {};
  }
  const std::streamsize size = file.tellg();
  if (size <= 0 || (size % 4) != 0) {
    std::cerr << "Invalid Vulkan projection shader size: " << path
              << std::endl;
    return {};
  }
  std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4u);
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char*>(words.data()), size);
  return words;
}

std::string ShaderPath(const char* name)
{
#ifdef PARTICLE_VIS_VULKAN_SHADER_DIR
  return std::string(PARTICLE_VIS_VULKAN_SHADER_DIR) + "/" + name;
#else
  return std::string(name);
#endif
}

#ifdef __APPLE__
void ConfigureMoltenVkIcdPath()
{
  if (std::getenv("VK_ICD_FILENAMES")) {
    return;
  }
  const std::array<const char*, 2> candidates = {
    "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json",
    "/usr/local/etc/vulkan/icd.d/MoltenVK_icd.json"
  };
  for (const char* candidate : candidates) {
    if (std::filesystem::exists(candidate)) {
      setenv("VK_ICD_FILENAMES", candidate, 0);
      return;
    }
  }
}

void* OpenMoltenVkLibrary()
{
  const std::array<const char*, 2> candidates = {
    "/opt/homebrew/lib/libMoltenVK.dylib",
    "/usr/local/lib/libMoltenVK.dylib"
  };
  for (const char* candidate : candidates) {
    if (std::filesystem::exists(candidate)) {
      if (void* handle = dlopen(candidate, RTLD_NOW | RTLD_LOCAL)) {
        return handle;
      }
    }
  }
  return nullptr;
}
#endif

class VulkanProjectionContext {
public:
  ~VulkanProjectionContext() { destroy(); }

  bool init()
  {
    if (device_ != VK_NULL_HANDLE) {
      return true;
    }

#ifdef __APPLE__
    ConfigureMoltenVkIcdPath();
    moltenVkLibrary_ = OpenMoltenVkLibrary();
#endif

    if (!createInstance() || !createDevice() || !createCommandPool() ||
        !createDescriptorResources()) {
      destroy();
      return false;
    }
    return true;
  }

  bool initFromExternal(VulkanContext* context)
  {
    if (!context || context->device() == VK_NULL_HANDLE ||
        context->physicalDevice() == VK_NULL_HANDLE ||
        context->queue() == VK_NULL_HANDLE ||
        context->queueFamily() == UINT32_MAX) {
      return init();
    }

    if (!ownsVulkanHandles_ && device_ == context->device()) {
      destroyProjectionResources();
    }

    destroy();
    instance_ = context->instance();
    physicalDevice_ = context->physicalDevice();
    device_ = context->device();
    queue_ = context->queue();
    queueFamily_ = context->queueFamily();
    shaderBufferFloat32AtomicAdd_ = context->shaderBufferFloat32AtomicAdd();
    ownsVulkanHandles_ = false;
    if (!createCommandPool() || !createDescriptorResources()) {
      destroy();
      return false;
    }
    return true;
  }

  VulkanBuffer createBuffer(VkDeviceSize size,
                            VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags properties)
  {
    VulkanBuffer out;
    out.size = size;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!Check(vkCreateBuffer(device_, &bufferInfo, nullptr, &out.buffer),
               "vkCreateBuffer(projection)")) {
      out.buffer = VK_NULL_HANDLE;
      return out;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, out.buffer, &requirements);
    const std::uint32_t memoryType =
      FindMemoryType(physicalDevice_,
                     requirements.memoryTypeBits,
                     properties);
    if (memoryType == UINT32_MAX) {
      std::cerr << "No suitable Vulkan projection memory type." << std::endl;
      destroyBuffer(out);
      return {};
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = memoryType;
    if (!Check(vkAllocateMemory(device_, &allocInfo, nullptr, &out.memory),
               "vkAllocateMemory(projection)")) {
      destroyBuffer(out);
      return {};
    }
    vkBindBufferMemory(device_, out.buffer, out.memory, 0);
    return out;
  }

  bool writeBuffer(const VulkanBuffer& buffer, const void* data, VkDeviceSize size)
  {
    void* mapped = nullptr;
    if (!Check(vkMapMemory(device_, buffer.memory, 0, size, 0, &mapped),
               "vkMapMemory(write projection)")) {
      return false;
    }
    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    vkUnmapMemory(device_, buffer.memory);
    return true;
  }

  bool readBuffer(const VulkanBuffer& buffer, void* data, VkDeviceSize size)
  {
    void* mapped = nullptr;
    if (!Check(vkMapMemory(device_, buffer.memory, 0, size, 0, &mapped),
               "vkMapMemory(read projection)")) {
      return false;
    }
    std::memcpy(data, mapped, static_cast<std::size_t>(size));
    vkUnmapMemory(device_, buffer.memory);
    return true;
  }

  void destroyBuffer(VulkanBuffer& buffer)
  {
    if (buffer.memory != VK_NULL_HANDLE) {
      vkFreeMemory(device_, buffer.memory, nullptr);
    }
    if (buffer.buffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, buffer.buffer, nullptr);
    }
    buffer = {};
  }

  VkDevice device() const { return device_; }
  VkQueue queue() const { return queue_; }
  VkCommandPool commandPool() const { return commandPool_; }
  VkDescriptorSetLayout descriptorSetLayout() const
  {
    return descriptorSetLayout_;
  }
  VkPipelineLayout pipelineLayout() const { return pipelineLayout_; }
  VkDescriptorPool descriptorPool() const { return descriptorPool_; }
  bool shaderBufferFloat32AtomicAdd() const
  {
    return shaderBufferFloat32AtomicAdd_;
  }
  void reset() { destroy(); }
  void resetReusableProjectionResources() { destroyProjectionResources(); }
  void freeDescriptorSets(const std::vector<VkDescriptorSet>& sets)
  {
    if (descriptorPool_ != VK_NULL_HANDLE && !sets.empty()) {
      vkFreeDescriptorSets(device_,
                           descriptorPool_,
                           static_cast<std::uint32_t>(sets.size()),
                           sets.data());
    }
  }

  VkPipeline pipeline(const char* shaderName)
  {
    if (std::strcmp(shaderName, "projection_sph.comp.spv") == 0) {
      if (sphPipeline_ == VK_NULL_HANDLE) {
        sphPipeline_ = createComputePipeline(shaderName);
      }
      return sphPipeline_;
    }
    if (std::strcmp(shaderName, "projection_voronoi_jfa.comp.spv") == 0) {
      if (jfaPipeline_ == VK_NULL_HANDLE) {
        jfaPipeline_ = createComputePipeline(shaderName);
      }
      return jfaPipeline_;
    }
    if (std::strcmp(shaderName, "projection_voronoi_render.comp.spv") == 0) {
      if (renderPipeline_ == VK_NULL_HANDLE) {
        renderPipeline_ = createComputePipeline(shaderName);
      }
      return renderPipeline_;
    }
    if (integratePipeline_ == VK_NULL_HANDLE) {
      integratePipeline_ = createComputePipeline(shaderName);
    }
    return integratePipeline_;
  }

  VkDescriptorSet allocateDescriptorSet()
  {
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout_;
    if (!Check(vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet),
               "vkAllocateDescriptorSets(projection)")) {
      return VK_NULL_HANDLE;
    }
    return descriptorSet;
  }

  void updateDescriptorSet(VkDescriptorSet descriptorSet,
                           const VulkanBuffer& b0,
                           const VulkanBuffer& b1,
                           const VulkanBuffer& b2,
                           const VulkanBuffer& b3,
                           const VulkanBuffer& b4)
  {
    std::array<VkDescriptorBufferInfo, 5> infos = {{
      {b0.buffer, 0, b0.size},
      {b1.buffer, 0, b1.size},
      {b2.buffer, 0, b2.size},
      {b3.buffer, 0, b3.size},
      {b4.buffer, 0, b4.size},
    }};
    std::array<VkWriteDescriptorSet, 5> writes{};
    for (std::uint32_t i = 0; i < writes.size(); ++i) {
      writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet = descriptorSet;
      writes[i].dstBinding = i;
      writes[i].descriptorCount = 1;
      writes[i].descriptorType =
        (i == 1)
          ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
          : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device_,
                           static_cast<std::uint32_t>(writes.size()),
                           writes.data(),
                           0,
                           nullptr);
  }

  void updateDescriptorSet(VkDescriptorSet descriptorSet,
                           const VulkanBuffer& b0,
                           const VulkanBuffer& b1,
                           const VulkanBuffer& b2,
                           const VulkanBuffer& b3,
                           const VulkanBuffer& b4,
                           const VulkanBuffer& b5)
  {
    std::array<VkDescriptorBufferInfo, 6> infos = {{
      {b0.buffer, 0, b0.size},
      {b1.buffer, 0, b1.size},
      {b2.buffer, 0, b2.size},
      {b3.buffer, 0, b3.size},
      {b4.buffer, 0, b4.size},
      {b5.buffer, 0, b5.size},
    }};
    std::array<VkWriteDescriptorSet, 6> writes{};
    for (std::uint32_t i = 0; i < writes.size(); ++i) {
      writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet = descriptorSet;
      writes[i].dstBinding = i;
      writes[i].descriptorCount = 1;
      writes[i].descriptorType =
        (i == 1)
          ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
          : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device_,
                           static_cast<std::uint32_t>(writes.size()),
                           writes.data(),
                           0,
                           nullptr);
  }

  void updateDescriptorSet(VkDescriptorSet descriptorSet,
                           const VulkanBuffer& b0,
                           const VulkanBuffer& b1,
                           const VulkanBuffer& b2,
                           const VulkanBuffer& b3,
                           const VulkanBuffer& b4,
                           const VulkanBuffer& b5,
                           const VulkanBuffer& b6)
  {
    std::array<VkDescriptorBufferInfo, 7> infos = {{
      {b0.buffer, 0, b0.size},
      {b1.buffer, 0, b1.size},
      {b2.buffer, 0, b2.size},
      {b3.buffer, 0, b3.size},
      {b4.buffer, 0, b4.size},
      {b5.buffer, 0, b5.size},
      {b6.buffer, 0, b6.size},
    }};
    std::array<VkWriteDescriptorSet, 7> writes{};
    for (std::uint32_t i = 0; i < writes.size(); ++i) {
      writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet = descriptorSet;
      writes[i].dstBinding = i;
      writes[i].descriptorCount = 1;
      writes[i].descriptorType =
        (i == 1)
          ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
          : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device_,
                           static_cast<std::uint32_t>(writes.size()),
                           writes.data(),
                           0,
                           nullptr);
  }

private:
  bool createInstance()
  {
    const auto available = EnumerateInstanceExtensions();
    std::vector<const char*> extensions;
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#if defined(__APPLE__) && defined(VK_LUNARG_DIRECT_DRIVER_LOADING_EXTENSION_NAME)
    VkDirectDriverLoadingInfoLUNARG directDriver{};
    VkDirectDriverLoadingListLUNARG directDriverList{};
    const char* directDriverEnv =
      std::getenv("PARTICLE_VIS_VULKAN_PROJECTION_DIRECT_DRIVER");
    if (directDriverEnv && std::strcmp(directDriverEnv, "1") == 0 &&
        IsExtensionAvailable(available,
                             VK_LUNARG_DIRECT_DRIVER_LOADING_EXTENSION_NAME)) {
      if (!moltenVkLibrary_) {
        moltenVkLibrary_ = OpenMoltenVkLibrary();
      }
      if (moltenVkLibrary_) {
        auto getProcAddr =
          reinterpret_cast<PFN_vkGetInstanceProcAddrLUNARG>(
            dlsym(moltenVkLibrary_, "vk_icdGetInstanceProcAddr"));
        if (!getProcAddr) {
          getProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddrLUNARG>(
            dlsym(moltenVkLibrary_, "vkGetInstanceProcAddr"));
        }
        if (getProcAddr) {
          AppendExtensionIfAvailable(extensions,
                                     available,
                                     VK_LUNARG_DIRECT_DRIVER_LOADING_EXTENSION_NAME);
          directDriver.sType =
            VK_STRUCTURE_TYPE_DIRECT_DRIVER_LOADING_INFO_LUNARG;
          directDriver.pfnGetInstanceProcAddr = getProcAddr;
          directDriverList.sType =
            VK_STRUCTURE_TYPE_DIRECT_DRIVER_LOADING_LIST_LUNARG;
          directDriverList.mode =
            VK_DIRECT_DRIVER_LOADING_MODE_INCLUSIVE_LUNARG;
          directDriverList.driverCount = 1;
          directDriverList.pDrivers = &directDriver;
          createInfo.pNext = &directDriverList;
        }
      }
    }
#endif
    AppendExtensionIfAvailable(
      extensions,
      available,
      VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    AppendExtensionIfAvailable(extensions,
                               available,
                               VK_KHR_SURFACE_EXTENSION_NAME);
    AppendExtensionIfAvailable(extensions,
                               available,
                               "VK_EXT_metal_surface");
    AppendExtensionIfAvailable(extensions,
                               available,
                               "VK_MVK_macos_surface");
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if (IsExtensionAvailable(available,
                             VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
      extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }
#endif

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "particle_vis_projection";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount =
      static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
#ifdef VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    return Check(vkCreateInstance(&createInfo, nullptr, &instance_),
                 "vkCreateInstance(projection)");
  }

  bool createDevice()
  {
    shaderBufferFloat32AtomicAdd_ = false;
    std::uint32_t deviceCount = 0;
    if (!Check(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr),
               "vkEnumeratePhysicalDevices(projection)") ||
        deviceCount == 0) {
      return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    for (VkPhysicalDevice candidate : devices) {
      std::uint32_t familyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
      std::vector<VkQueueFamilyProperties> families(familyCount);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate,
                                               &familyCount,
                                               families.data());
      for (std::uint32_t i = 0; i < familyCount; ++i) {
        if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
          physicalDevice_ = candidate;
          queueFamily_ = i;
          break;
        }
      }
      if (physicalDevice_ != VK_NULL_HANDLE) {
        break;
      }
    }
    if (physicalDevice_ == VK_NULL_HANDLE) {
      std::cerr << "No Vulkan compute queue found for projection."
                << std::endl;
      return false;
    }

    std::vector<const char*> extensions;
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    if (HasDeviceExtension(physicalDevice_,
                           VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
      extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }
#endif
#if defined(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME) && \
  defined(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT)
    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomicFloatFeatures{};
    atomicFloatFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT;
    bool enableShaderBufferFloat32AtomicAdd = false;
    if (HasDeviceExtension(physicalDevice_,
                           VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME)) {
      VkPhysicalDeviceFeatures2 features2{};
      features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
      features2.pNext = &atomicFloatFeatures;
      vkGetPhysicalDeviceFeatures2(physicalDevice_, &features2);
      if (atomicFloatFeatures.shaderBufferFloat32AtomicAdd == VK_TRUE) {
        extensions.push_back(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
        enableShaderBufferFloat32AtomicAdd = true;
      }
    }
#endif

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.enabledExtensionCount =
      static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
#if defined(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME) && \
  defined(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT)
    if (enableShaderBufferFloat32AtomicAdd) {
      createInfo.pNext = &atomicFloatFeatures;
    }
#endif

    if (!Check(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_),
               "vkCreateDevice(projection)")) {
      return false;
    }
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
#if defined(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME) && \
  defined(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT)
    shaderBufferFloat32AtomicAdd_ = enableShaderBufferFloat32AtomicAdd;
#endif

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    std::cerr << "Vulkan projection device: " << props.deviceName
              << std::endl;
    return true;
  }

  bool createCommandPool()
  {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                     VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily_;
    return Check(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_),
                 "vkCreateCommandPool(projection)");
  }

  bool createDescriptorResources()
  {
    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};
    for (std::uint32_t i = 0; i < bindings.size(); ++i) {
      bindings[i].binding = i;
      bindings[i].descriptorCount = 1;
      bindings[i].descriptorType =
        (i == 1)
          ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
          : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (!Check(vkCreateDescriptorSetLayout(device_,
                                           &layoutInfo,
                                           nullptr,
                                           &descriptorSetLayout_),
               "vkCreateDescriptorSetLayout(projection)")) {
      return false;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    if (!Check(vkCreatePipelineLayout(device_,
                                      &pipelineLayoutInfo,
                                      nullptr,
                                      &pipelineLayout_),
               "vkCreatePipelineLayout(projection)")) {
      return false;
    }

    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128},
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 4096;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    return Check(vkCreateDescriptorPool(device_,
                                        &poolInfo,
                                        nullptr,
                                        &descriptorPool_),
                 "vkCreateDescriptorPool(projection)");
  }

  VkPipeline createComputePipeline(const char* shaderName)
  {
    const std::vector<std::uint32_t> code = ReadSpirv(ShaderPath(shaderName));
    if (code.empty()) {
      return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size() * sizeof(std::uint32_t);
    moduleInfo.pCode = code.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (!Check(vkCreateShaderModule(device_, &moduleInfo, nullptr, &module),
               "vkCreateShaderModule(projection)")) {
      return VK_NULL_HANDLE;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout_;
    VkPipeline pipeline = VK_NULL_HANDLE;
    Check(vkCreateComputePipelines(device_,
                                   VK_NULL_HANDLE,
                                   1,
                                   &pipelineInfo,
                                   nullptr,
                                   &pipeline),
          "vkCreateComputePipelines(projection)");
    vkDestroyShaderModule(device_, module, nullptr);
    return pipeline;
  }

  void destroy()
  {
    if (device_ != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(device_);
      destroyProjectionResources();
      if (ownsVulkanHandles_) {
        vkDestroyDevice(device_, nullptr);
      }
    }
    if (ownsVulkanHandles_ && instance_ != VK_NULL_HANDLE) {
      vkDestroyInstance(instance_, nullptr);
    }
#ifdef __APPLE__
    if (moltenVkLibrary_) {
      dlclose(moltenVkLibrary_);
    }
#endif
    instance_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    queueFamily_ = UINT32_MAX;
    shaderBufferFloat32AtomicAdd_ = false;
    ownsVulkanHandles_ = true;
#ifdef __APPLE__
    moltenVkLibrary_ = nullptr;
#endif
  }

  void destroyProjectionResources()
  {
    if (device_ != VK_NULL_HANDLE) {
      if (jfaPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, jfaPipeline_, nullptr);
      }
      if (sphPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, sphPipeline_, nullptr);
      }
      if (integratePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, integratePipeline_, nullptr);
      }
      if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
      }
      if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
      }
      if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
      }
      if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
      }
    }
    commandPool_ = VK_NULL_HANDLE;
    descriptorSetLayout_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    sphPipeline_ = VK_NULL_HANDLE;
    jfaPipeline_ = VK_NULL_HANDLE;
    integratePipeline_ = VK_NULL_HANDLE;
    renderPipeline_ = VK_NULL_HANDLE;
  }

  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  std::uint32_t queueFamily_ = UINT32_MAX;
  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
  VkPipeline sphPipeline_ = VK_NULL_HANDLE;
  VkPipeline jfaPipeline_ = VK_NULL_HANDLE;
  VkPipeline integratePipeline_ = VK_NULL_HANDLE;
  VkPipeline renderPipeline_ = VK_NULL_HANDLE;
  bool shaderBufferFloat32AtomicAdd_ = false;
  bool ownsVulkanHandles_ = true;
#ifdef __APPLE__
  void* moltenVkLibrary_ = nullptr;
#endif
};

VulkanContext*& ActiveExternalContext()
{
  static VulkanContext* context = nullptr;
  return context;
}

VulkanProjectionContext& Context()
{
  static VulkanProjectionContext context;
  return context;
}

std::vector<VulkanProjectionParticle>
BuildVulkanParticles(const ProjectionGpuMapInput& input)
{
  std::vector<VulkanProjectionParticle> out;
  out.reserve(input.particles.size());
  for (const ProjectionGpuParticle& p : input.particles) {
    VulkanProjectionParticle vp;
    vp.posVal[0] = p.pos[0];
    vp.posVal[1] = p.pos[1];
    vp.posVal[2] = p.pos[2];
    vp.posVal[3] = p.val;
    vp.densityMassHsmlPad[0] = p.density;
    vp.densityMassHsmlPad[1] = p.mass;
    vp.densityMassHsmlPad[2] = p.hsml;
    vp.densityMassHsmlPad[3] = p.colorVal;
    out.push_back(vp);
  }
  return out;
}

bool StandaloneVulkanProjectionAllowed(std::string* reason)
{
  if (reason) {
    reason->clear();
  }
  return true;
}

VulkanVoronoiUniforms BuildUniforms(const ProjectionGpuMapInput& input)
{
  VulkanVoronoiUniforms uniforms;
  uniforms.width = static_cast<std::uint32_t>(input.width);
  uniforms.height = static_cast<std::uint32_t>(input.height);
  uniforms.depth = static_cast<std::uint32_t>(input.depth);
  uniforms.particleCount = static_cast<std::uint32_t>(input.particles.size());
  uniforms.dx = input.dx;
  uniforms.dy = input.dy;
  uniforms.dz = input.dz;
  uniforms.xminX = input.xminLocal[0];
  uniforms.xminY = input.xminLocal[1];
  uniforms.xminZ = input.xminLocal[2];
  uniforms.densityWeight = input.densityWeight ? 1u : 0u;
  uniforms.renderColor[0] = input.renderColor[0];
  uniforms.renderColor[1] = input.renderColor[1];
  uniforms.renderColor[2] = input.renderColor[2];
  uniforms.tfCount = static_cast<std::uint32_t>(
    std::min<std::size_t>(input.transferComponents.size(),
                          kProjectionGpuMaxTfComponents));
  uniforms.colorMapSize = static_cast<std::uint32_t>(input.colorMapSize);
  uniforms.colorLogScale = input.colorLogScale ? 1u : 0u;
  uniforms.colorValueMin = input.colorValueMin;
  uniforms.colorValueMax = input.colorValueMax;
  return uniforms;
}

VulkanProjectionUniforms BuildProjectionUniforms(
  const ProjectionGpuMapInput& input)
{
  VulkanProjectionUniforms uniforms;
  uniforms.width = static_cast<std::uint32_t>(input.width);
  uniforms.height = static_cast<std::uint32_t>(input.height);
  uniforms.densityWeight = input.densityWeight ? 1u : 0u;
  uniforms.particleCount =
    static_cast<std::uint32_t>(input.particles.size());
  uniforms.dx = input.dx;
  uniforms.dy = input.dy;
  uniforms.xminX = input.xminLocal[0];
  uniforms.xminY = input.xminLocal[1];
  uniforms.center[0] = input.center.x;
  uniforms.center[1] = input.center.y;
  uniforms.center[2] = input.center.z;
  uniforms.uAxis[0] = input.uAxis.x;
  uniforms.uAxis[1] = input.uAxis.y;
  uniforms.uAxis[2] = input.uAxis.z;
  uniforms.vAxis[0] = input.vAxis.x;
  uniforms.vAxis[1] = input.vAxis.y;
  uniforms.vAxis[2] = input.vAxis.z;
  uniforms.valueMin = input.valueMin;
  uniforms.valueMax = input.valueMax;
  return uniforms;
}

VkCommandBuffer BeginOneShotCommand(VulkanProjectionContext& ctx)
{
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = ctx.commandPool();
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;
  vkAllocateCommandBuffers(ctx.device(), &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(commandBuffer, &beginInfo);
  return commandBuffer;
}

bool EndSubmitWait(VulkanProjectionContext& ctx, VkCommandBuffer commandBuffer)
{
  vkEndCommandBuffer(commandBuffer);
  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  const bool ok = Check(vkQueueSubmit(ctx.queue(),
                                      1,
                                      &submitInfo,
                                      VK_NULL_HANDLE),
                        "vkQueueSubmit(projection)") &&
                  Check(vkQueueWaitIdle(ctx.queue()),
                        "vkQueueWaitIdle(projection)");
  vkFreeCommandBuffers(ctx.device(), ctx.commandPool(), 1, &commandBuffer);
  return ok;
}

void ShaderWriteBarrier(VkCommandBuffer commandBuffer,
                        VkBuffer buffer,
                        VkDeviceSize size)
{
  VkBufferMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                          VK_ACCESS_SHADER_WRITE_BIT;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = buffer;
  barrier.offset = 0;
  barrier.size = size;
  vkCmdPipelineBarrier(commandBuffer,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       0,
                       0,
                       nullptr,
                       1,
                       &barrier,
                       0,
                       nullptr);
}

void HostReadBarrier(VkCommandBuffer commandBuffer,
                     VkBuffer buffer,
                     VkDeviceSize size)
{
  VkBufferMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = buffer;
  barrier.offset = 0;
  barrier.size = size;
  vkCmdPipelineBarrier(commandBuffer,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT,
                       0,
                       0,
                       nullptr,
                       1,
                       &barrier,
                       0,
                       nullptr);
}

void Dispatch1D(VkCommandBuffer commandBuffer, std::size_t count)
{
  constexpr std::uint32_t kLocalSize = 256;
  const std::uint32_t groups =
    static_cast<std::uint32_t>((count + kLocalSize - 1u) / kLocalSize);
  vkCmdDispatch(commandBuffer, groups, 1, 1);
}

#endif

} // namespace

bool IsVulkanProjectionBackendAvailable(std::string* reason)
{
#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
  VulkanProjectionContext& context = Context();
  const bool hasExternal = ActiveExternalContext() != nullptr;
  if (!hasExternal && !StandaloneVulkanProjectionAllowed(reason)) {
    return false;
  }
  if (!(hasExternal ? context.initFromExternal(ActiveExternalContext())
                    : context.init())) {
    if (reason) {
      *reason = "Vulkan projection context initialization failed.";
    }
    return false;
  }
  if (reason) {
    *reason = "Vulkan projection context is available.";
  }
  return true;
#else
  if (reason) {
    *reason = "Vulkan backend was not compiled into this build.";
  }
  return false;
#endif
}

void SetVulkanProjectionContext(VulkanContext* context)
{
#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
  if (ActiveExternalContext() != context) {
    Context().reset();
    ActiveExternalContext() = context;
  }
#else
  (void)context;
#endif
}

bool RunVulkanProjectionMap(const ProjectionGpuMapInput& input,
                            ProjectionGpuMapOutput& output)
{
  output = ProjectionGpuMapOutput{};
#ifndef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
  (void)input;
  std::cerr << "Vulkan backend was not compiled into this build." << std::endl;
  return false;
#else
  if (input.width <= 0 || input.height <= 0 || input.dx <= 0.0f ||
      input.dy <= 0.0f || input.particles.empty()) {
    return false;
  }
  std::string availabilityReason;
  const bool hasExternal = ActiveExternalContext() != nullptr;
  if (!hasExternal &&
      !StandaloneVulkanProjectionAllowed(&availabilityReason)) {
    std::cerr << "Vulkan SPH projection unavailable: "
              << availabilityReason << std::endl;
    return false;
  }
  VulkanProjectionContext& ctx = Context();
  if (!(hasExternal ? ctx.initFromExternal(ActiveExternalContext())
                    : ctx.init())) {
    return false;
  }
  if (!ctx.shaderBufferFloat32AtomicAdd()) {
    std::cerr
      << "Vulkan SPH projection unavailable: "
      << "shaderBufferFloat32AtomicAdd is not supported by this device; "
      << "falling back to CPU projection."
      << std::endl;
    return false;
  }

  const std::size_t pixelCount =
    static_cast<std::size_t>(input.width) *
    static_cast<std::size_t>(input.height);
  const std::size_t outputBytes = pixelCount * sizeof(float);
  const auto vulkanParticles = BuildVulkanParticles(input);
  const std::size_t particleBytes =
    vulkanParticles.size() * sizeof(VulkanProjectionParticle);
  const VulkanProjectionUniforms uniforms = BuildProjectionUniforms(input);
  const std::vector<float> zeroOutput(pixelCount, 0.0f);
  const VulkanStepUniform dummyUniform{};

  VulkanBuffer particleBuffer = ctx.createBuffer(
    particleBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer uniformBuffer = ctx.createBuffer(
    sizeof(uniforms),
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer valueBuffer = ctx.createBuffer(
    outputBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer weightBuffer = ctx.createBuffer(
    outputBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer dummyBuffer = ctx.createBuffer(
    sizeof(dummyUniform),
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (particleBuffer.buffer == VK_NULL_HANDLE ||
      uniformBuffer.buffer == VK_NULL_HANDLE ||
      valueBuffer.buffer == VK_NULL_HANDLE ||
      weightBuffer.buffer == VK_NULL_HANDLE ||
      dummyBuffer.buffer == VK_NULL_HANDLE ||
      !ctx.writeBuffer(particleBuffer,
                       vulkanParticles.data(),
                       static_cast<VkDeviceSize>(particleBytes)) ||
      !ctx.writeBuffer(uniformBuffer, &uniforms, sizeof(uniforms)) ||
      !ctx.writeBuffer(valueBuffer, zeroOutput.data(), outputBytes) ||
      !ctx.writeBuffer(weightBuffer, zeroOutput.data(), outputBytes) ||
      !ctx.writeBuffer(dummyBuffer, &dummyUniform, sizeof(dummyUniform))) {
    ctx.destroyBuffer(particleBuffer);
    ctx.destroyBuffer(uniformBuffer);
    ctx.destroyBuffer(valueBuffer);
    ctx.destroyBuffer(weightBuffer);
    ctx.destroyBuffer(dummyBuffer);
    return false;
  }

  VkPipeline pipeline = ctx.pipeline("projection_sph.comp.spv");
  if (pipeline == VK_NULL_HANDLE) {
    ctx.destroyBuffer(particleBuffer);
    ctx.destroyBuffer(uniformBuffer);
    ctx.destroyBuffer(valueBuffer);
    ctx.destroyBuffer(weightBuffer);
    ctx.destroyBuffer(dummyBuffer);
    return false;
  }

  VkCommandBuffer commandBuffer = BeginOneShotCommand(ctx);
  VkDescriptorSet set = ctx.allocateDescriptorSet();
  if (set == VK_NULL_HANDLE) {
    vkFreeCommandBuffers(ctx.device(), ctx.commandPool(), 1, &commandBuffer);
    ctx.destroyBuffer(particleBuffer);
    ctx.destroyBuffer(uniformBuffer);
    ctx.destroyBuffer(valueBuffer);
    ctx.destroyBuffer(weightBuffer);
    ctx.destroyBuffer(dummyBuffer);
    return false;
  }
  ctx.updateDescriptorSet(set,
                          particleBuffer,
                          uniformBuffer,
                          valueBuffer,
                          weightBuffer,
                          dummyBuffer);
  const auto start = std::chrono::steady_clock::now();
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
  vkCmdBindDescriptorSets(commandBuffer,
                          VK_PIPELINE_BIND_POINT_COMPUTE,
                          ctx.pipelineLayout(),
                          0,
                          1,
                          &set,
                          0,
                          nullptr);
  Dispatch1D(commandBuffer, input.particles.size());
  HostReadBarrier(commandBuffer, valueBuffer.buffer, valueBuffer.size);
  HostReadBarrier(commandBuffer, weightBuffer.buffer, weightBuffer.size);
  const bool submitted = EndSubmitWait(ctx, commandBuffer);
  const auto end = std::chrono::steady_clock::now();
  ctx.freeDescriptorSets(std::vector<VkDescriptorSet>{set});
  if (!submitted) {
    ctx.destroyBuffer(particleBuffer);
    ctx.destroyBuffer(uniformBuffer);
    ctx.destroyBuffer(valueBuffer);
    ctx.destroyBuffer(weightBuffer);
    ctx.destroyBuffer(dummyBuffer);
    return false;
  }

  output.values.resize(pixelCount);
  output.weights.resize(pixelCount);
  const bool readValues = ctx.readBuffer(valueBuffer,
                                        output.values.data(),
                                        outputBytes);
  const bool readWeights = ctx.readBuffer(weightBuffer,
                                         output.weights.data(),
                                         outputBytes);
  output.elapsedMs =
    std::chrono::duration<double, std::milli>(end - start).count();

  std::size_t nonzeroWeights = 0;
  double weightSum = 0.0;
  float weightMax = 0.0f;
  float valueMin = std::numeric_limits<float>::max();
  float valueMax = -std::numeric_limits<float>::max();
  for (std::size_t i = 0; i < pixelCount; ++i) {
    const float weight = output.weights[i];
    if (weight > 0.0f) {
      ++nonzeroWeights;
      weightSum += weight;
      weightMax = std::max(weightMax, weight);
      valueMin = std::min(valueMin, output.values[i]);
      valueMax = std::max(valueMax, output.values[i]);
    }
  }
  std::cout << "Vulkan SPH projection compute readback: particles="
            << input.particles.size()
            << " pixels=" << input.width << "x" << input.height
            << " nonzeroWeights=" << nonzeroWeights
            << " weightSum=" << weightSum
            << " weightMax=" << weightMax;
  if (nonzeroWeights > 0) {
    std::cout << " valueAccumRange=[" << valueMin << ", " << valueMax << "]";
  }
  std::cout << " elapsedMs=" << output.elapsedMs << std::endl;

  ctx.destroyBuffer(particleBuffer);
  ctx.destroyBuffer(uniformBuffer);
  ctx.destroyBuffer(valueBuffer);
  ctx.destroyBuffer(weightBuffer);
  ctx.destroyBuffer(dummyBuffer);
  ctx.resetReusableProjectionResources();
  return readValues && readWeights && nonzeroWeights > 0;
#endif
}

bool BuildVulkanVoronoiLabelGrid(const ProjectionGpuMapInput& input,
                                 ProjectionGpuLabelGrid& grid)
{
  grid = ProjectionGpuLabelGrid{};
#ifndef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
  (void)input;
  std::cerr << "Vulkan backend was not compiled into this build." << std::endl;
  return false;
#else
  if (input.width <= 0 || input.height <= 0 || input.depth <= 0 ||
      input.dx <= 0.0f || input.dy <= 0.0f || input.dz <= 0.0f ||
      input.particles.empty()) {
    return false;
  }
  std::string availabilityReason;
  const bool hasExternal = ActiveExternalContext() != nullptr;
  if (!hasExternal &&
      !StandaloneVulkanProjectionAllowed(&availabilityReason)) {
    std::cerr << "Vulkan Voronoi projection unavailable: "
              << availabilityReason << std::endl;
    return false;
  }
  VulkanProjectionContext& ctx = Context();
  if (!(hasExternal ? ctx.initFromExternal(ActiveExternalContext())
                    : ctx.init())) {
    return false;
  }

  const std::size_t pixelCount =
    static_cast<std::size_t>(input.width) *
    static_cast<std::size_t>(input.height);
  const std::size_t voxelCount =
    pixelCount * static_cast<std::size_t>(input.depth);
  const std::size_t labelBytes = voxelCount * sizeof(std::int32_t);
  const auto vulkanParticles = BuildVulkanParticles(input);
  const std::size_t particleBytes =
    vulkanParticles.size() * sizeof(VulkanProjectionParticle);
  const VulkanVoronoiUniforms uniforms = BuildUniforms(input);

  std::vector<std::int32_t> seedLabels(voxelCount, -1);
  std::vector<float> seedDist2(voxelCount, std::numeric_limits<float>::infinity());
  const auto voxelIndex = [width = input.width, height = input.height](int i,
                                                                       int j,
                                                                       int k) {
    return static_cast<std::size_t>(k) *
             static_cast<std::size_t>(width) *
             static_cast<std::size_t>(height) +
           static_cast<std::size_t>(j) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(i);
  };

  for (std::size_t particleIndex = 0; particleIndex < input.particles.size();
       ++particleIndex) {
    const ProjectionGpuParticle& p = input.particles[particleIndex];
    const int i = static_cast<int>(
      std::lround((p.pos[0] - (input.xminLocal[0] + 0.5f * input.dx)) / input.dx));
    const int j = static_cast<int>(
      std::lround((p.pos[1] - (input.xminLocal[1] + 0.5f * input.dy)) / input.dy));
    const int k = static_cast<int>(
      std::lround((p.pos[2] - input.xminLocal[2]) / input.dz));
    if (i < 0 || i >= input.width || j < 0 || j >= input.height ||
        k < 0 || k >= input.depth) {
      continue;
    }
    const float cx = input.xminLocal[0] + (static_cast<float>(i) + 0.5f) * input.dx;
    const float cy = input.xminLocal[1] + (static_cast<float>(j) + 0.5f) * input.dy;
    const float cz = input.xminLocal[2] + static_cast<float>(k) * input.dz;
    const float dx = p.pos[0] - cx;
    const float dy = p.pos[1] - cy;
    const float dz = p.pos[2] - cz;
    const float dist2 = dx * dx + dy * dy + dz * dz;
    const std::size_t idx = voxelIndex(i, j, k);
    if (dist2 < seedDist2[idx]) {
      seedDist2[idx] = dist2;
      seedLabels[idx] = static_cast<std::int32_t>(particleIndex);
    }
  }

  const std::size_t seededVoxelCount =
    static_cast<std::size_t>(std::count_if(seedLabels.begin(),
                                           seedLabels.end(),
                                           [](std::int32_t label) {
                                             return label >= 0;
                                           }));
  if (seededVoxelCount == 0) {
    std::cerr << "Vulkan Voronoi projection seed grid is empty." << std::endl;
    return false;
  }

  VulkanBuffer particleBuffer = ctx.createBuffer(
    particleBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer uniformBuffer = ctx.createBuffer(
    sizeof(uniforms),
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer labelA = ctx.createBuffer(
    labelBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer labelB = ctx.createBuffer(
    labelBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (particleBuffer.buffer == VK_NULL_HANDLE ||
      uniformBuffer.buffer == VK_NULL_HANDLE ||
      labelA.buffer == VK_NULL_HANDLE || labelB.buffer == VK_NULL_HANDLE ||
      !ctx.writeBuffer(particleBuffer,
                       vulkanParticles.data(),
                       static_cast<VkDeviceSize>(particleBytes)) ||
      !ctx.writeBuffer(uniformBuffer, &uniforms, sizeof(uniforms)) ||
      !ctx.writeBuffer(labelA, seedLabels.data(), labelBytes)) {
    ctx.destroyBuffer(particleBuffer);
    ctx.destroyBuffer(uniformBuffer);
    ctx.destroyBuffer(labelA);
    ctx.destroyBuffer(labelB);
    return false;
  }

  VkPipeline pipeline = ctx.pipeline("projection_voronoi_jfa.comp.spv");
  if (pipeline == VK_NULL_HANDLE) {
    ctx.destroyBuffer(particleBuffer);
    ctx.destroyBuffer(uniformBuffer);
    ctx.destroyBuffer(labelA);
    ctx.destroyBuffer(labelB);
    return false;
  }

  int maxDim = std::max(input.width, std::max(input.height, input.depth));
  std::uint32_t step = 1u;
  while (step < static_cast<std::uint32_t>((maxDim + 1) / 2)) {
    step <<= 1u;
  }
  std::vector<std::uint32_t> steps;
  for (; step >= 1u; step >>= 1u) {
    steps.push_back(step);
    if (step == 1u) {
      break;
    }
  }
  steps.push_back(1u);
  steps.push_back(1u);

  std::vector<VulkanBuffer> stepBuffers;
  stepBuffers.reserve(steps.size());
  for (std::uint32_t passStep : steps) {
    VulkanBuffer stepBuffer = ctx.createBuffer(
      sizeof(VulkanStepUniform),
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VulkanStepUniform stepUniform;
    stepUniform.stepSize = passStep;
    if (stepBuffer.buffer == VK_NULL_HANDLE ||
        !ctx.writeBuffer(stepBuffer, &stepUniform, sizeof(stepUniform))) {
      for (VulkanBuffer& buffer : stepBuffers) {
        ctx.destroyBuffer(buffer);
      }
      ctx.destroyBuffer(stepBuffer);
      ctx.destroyBuffer(particleBuffer);
      ctx.destroyBuffer(uniformBuffer);
      ctx.destroyBuffer(labelA);
      ctx.destroyBuffer(labelB);
      return false;
    }
    stepBuffers.push_back(stepBuffer);
  }

  const auto start = std::chrono::steady_clock::now();
  VkCommandBuffer commandBuffer = BeginOneShotCommand(ctx);
  VulkanBuffer* currentLabels = &labelA;
  VulkanBuffer* nextLabels = &labelB;
  std::vector<VkDescriptorSet> descriptorSets;
  descriptorSets.reserve(steps.size());
  for (std::size_t passIndex = 0; passIndex < steps.size(); ++passIndex) {
    VkDescriptorSet set = ctx.allocateDescriptorSet();
    if (set == VK_NULL_HANDLE) {
      break;
    }
    descriptorSets.push_back(set);
    ctx.updateDescriptorSet(set,
                            particleBuffer,
                            uniformBuffer,
                            *currentLabels,
                            *nextLabels,
                            stepBuffers[passIndex]);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            ctx.pipelineLayout(),
                            0,
                            1,
                            &set,
                            0,
                            nullptr);
    Dispatch1D(commandBuffer, voxelCount);
    ShaderWriteBarrier(commandBuffer, nextLabels->buffer, nextLabels->size);
    std::swap(currentLabels, nextLabels);
  }
  HostReadBarrier(commandBuffer, currentLabels->buffer, currentLabels->size);
  const bool submitted = EndSubmitWait(ctx, commandBuffer);
  const auto end = std::chrono::steady_clock::now();
  ctx.freeDescriptorSets(descriptorSets);

  if (!submitted) {
    ctx.destroyBuffer(particleBuffer);
    ctx.destroyBuffer(uniformBuffer);
    ctx.destroyBuffer(labelA);
    ctx.destroyBuffer(labelB);
    for (VulkanBuffer& buffer : stepBuffers) {
      ctx.destroyBuffer(buffer);
    }
    return false;
  }

  grid.width = input.width;
  grid.height = input.height;
  grid.depth = input.depth;
  grid.labels.resize(voxelCount);
  const bool readOk = ctx.readBuffer(*currentLabels,
                                     grid.labels.data(),
                                     labelBytes);
  grid.elapsedMs =
    std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "Vulkan Voronoi JFA labels: particles="
            << input.particles.size()
            << " grid=" << input.width << "x" << input.height << "x"
            << input.depth
            << " seededVoxels=" << seededVoxelCount
            << " elapsedMs=" << grid.elapsedMs
            << std::endl;

  ctx.destroyBuffer(particleBuffer);
  ctx.destroyBuffer(uniformBuffer);
  ctx.destroyBuffer(labelA);
  ctx.destroyBuffer(labelB);
  for (VulkanBuffer& buffer : stepBuffers) {
    ctx.destroyBuffer(buffer);
  }
  ctx.resetReusableProjectionResources();
  return readOk;
#endif
}

bool IntegrateVulkanVoronoiLabelGrid(const ProjectionGpuMapInput& input,
                                     const ProjectionGpuLabelGrid& grid,
                                     ProjectionGpuMapOutput& output)
{
  output = ProjectionGpuMapOutput{};
#ifndef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
  (void)input;
  (void)grid;
  std::cerr << "Vulkan backend was not compiled into this build." << std::endl;
  return false;
#else
  if (input.width <= 0 || input.height <= 0 || input.depth <= 0 ||
      input.width != grid.width || input.height != grid.height ||
      input.depth != grid.depth || input.particles.empty() ||
      grid.labels.empty()) {
    return false;
  }
  std::string availabilityReason;
  const bool hasExternal = ActiveExternalContext() != nullptr;
  if (!hasExternal &&
      !StandaloneVulkanProjectionAllowed(&availabilityReason)) {
    std::cerr << "Vulkan Voronoi integration unavailable: "
              << availabilityReason << std::endl;
    return false;
  }
  VulkanProjectionContext& ctx = Context();
  if (!(hasExternal ? ctx.initFromExternal(ActiveExternalContext())
                    : ctx.init())) {
    return false;
  }

  const std::size_t pixelCount =
    static_cast<std::size_t>(input.width) *
    static_cast<std::size_t>(input.height);
  const std::size_t voxelCount =
    pixelCount * static_cast<std::size_t>(input.depth);
  const std::size_t outputBytes = pixelCount * sizeof(float);
  const std::size_t labelBytes = voxelCount * sizeof(std::int32_t);
  const auto vulkanParticles = BuildVulkanParticles(input);
  const std::size_t particleBytes =
    vulkanParticles.size() * sizeof(VulkanProjectionParticle);
  const VulkanVoronoiUniforms uniforms = BuildUniforms(input);

  VulkanBuffer particleBuffer = ctx.createBuffer(
    particleBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer uniformBuffer = ctx.createBuffer(
    sizeof(uniforms),
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer labelBuffer = ctx.createBuffer(
    labelBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer valueBuffer = ctx.createBuffer(
    outputBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer weightBuffer = ctx.createBuffer(
    outputBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (particleBuffer.buffer == VK_NULL_HANDLE ||
      uniformBuffer.buffer == VK_NULL_HANDLE ||
      labelBuffer.buffer == VK_NULL_HANDLE ||
      valueBuffer.buffer == VK_NULL_HANDLE ||
      weightBuffer.buffer == VK_NULL_HANDLE ||
      !ctx.writeBuffer(particleBuffer,
                       vulkanParticles.data(),
                       static_cast<VkDeviceSize>(particleBytes)) ||
      !ctx.writeBuffer(uniformBuffer, &uniforms, sizeof(uniforms)) ||
      !ctx.writeBuffer(labelBuffer, grid.labels.data(), labelBytes)) {
    ctx.destroyBuffer(particleBuffer);
    ctx.destroyBuffer(uniformBuffer);
    ctx.destroyBuffer(labelBuffer);
    ctx.destroyBuffer(valueBuffer);
    ctx.destroyBuffer(weightBuffer);
    return false;
  }

  VkPipeline pipeline = ctx.pipeline("projection_voronoi_integrate.comp.spv");
  if (pipeline == VK_NULL_HANDLE) {
    ctx.destroyBuffer(particleBuffer);
    ctx.destroyBuffer(uniformBuffer);
    ctx.destroyBuffer(labelBuffer);
    ctx.destroyBuffer(valueBuffer);
    ctx.destroyBuffer(weightBuffer);
    return false;
  }

  VkCommandBuffer commandBuffer = BeginOneShotCommand(ctx);
  VkDescriptorSet set = ctx.allocateDescriptorSet();
  ctx.updateDescriptorSet(set,
                          particleBuffer,
                          uniformBuffer,
                          labelBuffer,
                          valueBuffer,
                          weightBuffer);
  const auto start = std::chrono::steady_clock::now();
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
  vkCmdBindDescriptorSets(commandBuffer,
                          VK_PIPELINE_BIND_POINT_COMPUTE,
                          ctx.pipelineLayout(),
                          0,
                          1,
                          &set,
                          0,
                          nullptr);
  Dispatch1D(commandBuffer, pixelCount);
  HostReadBarrier(commandBuffer, valueBuffer.buffer, valueBuffer.size);
  HostReadBarrier(commandBuffer, weightBuffer.buffer, weightBuffer.size);
  const bool submitted = EndSubmitWait(ctx, commandBuffer);
  const auto end = std::chrono::steady_clock::now();
  ctx.freeDescriptorSets(std::vector<VkDescriptorSet>{set});
  if (!submitted) {
    ctx.destroyBuffer(particleBuffer);
    ctx.destroyBuffer(uniformBuffer);
    ctx.destroyBuffer(labelBuffer);
    ctx.destroyBuffer(valueBuffer);
    ctx.destroyBuffer(weightBuffer);
    return false;
  }

  output.values.resize(pixelCount);
  output.weights.resize(pixelCount);
  const bool readValues = ctx.readBuffer(valueBuffer,
                                        output.values.data(),
                                        outputBytes);
  const bool readWeights = ctx.readBuffer(weightBuffer,
                                         output.weights.data(),
                                         outputBytes);
  output.elapsedMs =
    std::chrono::duration<double, std::milli>(end - start).count();

  std::size_t nonzeroWeights = 0;
  double weightSum = 0.0;
  float weightMax = 0.0f;
  for (float weight : output.weights) {
    if (weight > 0.0f) {
      ++nonzeroWeights;
      weightSum += weight;
      weightMax = std::max(weightMax, weight);
    }
  }
  std::cout << "Vulkan Voronoi integration: particles="
            << input.particles.size()
            << " grid=" << input.width << "x" << input.height << "x"
            << input.depth
            << " nonzeroWeights=" << nonzeroWeights
            << " weightSum=" << weightSum
            << " weightMax=" << weightMax
            << " elapsedMs=" << output.elapsedMs
            << std::endl;

  ctx.destroyBuffer(particleBuffer);
  ctx.destroyBuffer(uniformBuffer);
  ctx.destroyBuffer(labelBuffer);
  ctx.destroyBuffer(valueBuffer);
  ctx.destroyBuffer(weightBuffer);
  ctx.resetReusableProjectionResources();
  return readValues && readWeights && nonzeroWeights > 0;
#endif
}

bool RenderVulkanVoronoiLabelGrid(const ProjectionGpuMapInput& input,
                                  const ProjectionGpuLabelGrid& grid,
                                  ProjectionGpuMapOutput& output)
{
  output = ProjectionGpuMapOutput{};
#ifndef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
  (void)input;
  (void)grid;
  std::cerr << "Vulkan backend was not compiled into this build." << std::endl;
  return false;
#else
  if (input.width <= 0 || input.height <= 0 || input.depth <= 0 ||
      input.width != grid.width || input.height != grid.height ||
      input.depth != grid.depth || input.particles.empty() ||
      grid.labels.empty() || input.transferComponents.empty()) {
    return false;
  }
  std::string availabilityReason;
  const bool hasExternal = ActiveExternalContext() != nullptr;
  if (!hasExternal &&
      !StandaloneVulkanProjectionAllowed(&availabilityReason)) {
    std::cerr << "Vulkan Voronoi render unavailable: "
              << availabilityReason << std::endl;
    return false;
  }
  VulkanProjectionContext& ctx = Context();
  if (!(hasExternal ? ctx.initFromExternal(ActiveExternalContext())
                    : ctx.init())) {
    return false;
  }

  const std::size_t pixelCount =
    static_cast<std::size_t>(input.width) *
    static_cast<std::size_t>(input.height);
  const std::size_t voxelCount =
    pixelCount * static_cast<std::size_t>(input.depth);
  const std::size_t rgbBytes = pixelCount * 3 * sizeof(float);
  const std::size_t alphaBytes = pixelCount * sizeof(float);
  const std::size_t labelBytes = voxelCount * sizeof(std::int32_t);
  const std::size_t colorMapBytes =
    input.colorMap.empty()
      ? 0
      : input.colorMap.size() * sizeof(float);
  const auto vulkanParticles = BuildVulkanParticles(input);
  const std::size_t particleBytes =
    vulkanParticles.size() * sizeof(VulkanProjectionParticle);
  const VulkanVoronoiUniforms uniforms = BuildUniforms(input);

  std::vector<VulkanProjectionTfComponent> tfComponents(
    std::min<std::size_t>(input.transferComponents.size(),
                          kProjectionGpuMaxTfComponents));
  for (std::size_t i = 0; i < tfComponents.size(); ++i) {
    const ProjectionGpuTransferComponent& src = input.transferComponents[i];
    tfComponents[i].type = static_cast<std::uint32_t>(std::clamp(src.type, 0, 2));
    tfComponents[i].center = src.center;
    tfComponents[i].width = src.width;
    tfComponents[i].amplitude = src.amplitude;
    tfComponents[i].logDomain = src.logDomain ? 1u : 0u;
  }
  const std::size_t tfBytes =
    tfComponents.size() * sizeof(VulkanProjectionTfComponent);

  VulkanBuffer particleBuffer = ctx.createBuffer(
    particleBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer uniformBuffer = ctx.createBuffer(
    sizeof(uniforms),
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer labelBuffer = ctx.createBuffer(
    labelBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer rgbBuffer = ctx.createBuffer(
    rgbBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer alphaBuffer = ctx.createBuffer(
    alphaBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer tfBuffer = ctx.createBuffer(
    tfBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VulkanBuffer colorMapBuffer = ctx.createBuffer(
    colorMapBytes,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (particleBuffer.buffer == VK_NULL_HANDLE ||
      uniformBuffer.buffer == VK_NULL_HANDLE ||
      labelBuffer.buffer == VK_NULL_HANDLE ||
      rgbBuffer.buffer == VK_NULL_HANDLE ||
      alphaBuffer.buffer == VK_NULL_HANDLE ||
      tfBuffer.buffer == VK_NULL_HANDLE ||
      colorMapBuffer.buffer == VK_NULL_HANDLE ||
      !ctx.writeBuffer(particleBuffer,
                       vulkanParticles.data(),
                       static_cast<VkDeviceSize>(particleBytes)) ||
      !ctx.writeBuffer(uniformBuffer, &uniforms, sizeof(uniforms)) ||
      !ctx.writeBuffer(labelBuffer, grid.labels.data(), labelBytes) ||
      !ctx.writeBuffer(tfBuffer, tfComponents.data(), tfBytes) ||
      !ctx.writeBuffer(colorMapBuffer, input.colorMap.data(), colorMapBytes)) {
    ctx.destroyBuffer(particleBuffer);
    ctx.destroyBuffer(uniformBuffer);
    ctx.destroyBuffer(labelBuffer);
    ctx.destroyBuffer(rgbBuffer);
    ctx.destroyBuffer(alphaBuffer);
    ctx.destroyBuffer(tfBuffer);
    ctx.destroyBuffer(colorMapBuffer);
    return false;
  }

  VkPipeline pipeline = ctx.pipeline("projection_voronoi_render.comp.spv");
  if (pipeline == VK_NULL_HANDLE) {
    ctx.destroyBuffer(particleBuffer);
    ctx.destroyBuffer(uniformBuffer);
    ctx.destroyBuffer(labelBuffer);
    ctx.destroyBuffer(rgbBuffer);
    ctx.destroyBuffer(alphaBuffer);
    ctx.destroyBuffer(tfBuffer);
    ctx.destroyBuffer(colorMapBuffer);
    return false;
  }

  VkCommandBuffer commandBuffer = BeginOneShotCommand(ctx);
  VkDescriptorSet set = ctx.allocateDescriptorSet();
  ctx.updateDescriptorSet(set,
                          particleBuffer,
                          uniformBuffer,
                          labelBuffer,
                          rgbBuffer,
                          alphaBuffer,
                          tfBuffer,
                          colorMapBuffer);
  const auto start = std::chrono::steady_clock::now();
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
  vkCmdBindDescriptorSets(commandBuffer,
                          VK_PIPELINE_BIND_POINT_COMPUTE,
                          ctx.pipelineLayout(),
                          0,
                          1,
                          &set,
                          0,
                          nullptr);
  Dispatch1D(commandBuffer, pixelCount);
  HostReadBarrier(commandBuffer, rgbBuffer.buffer, rgbBuffer.size);
  HostReadBarrier(commandBuffer, alphaBuffer.buffer, alphaBuffer.size);
  const bool submitted = EndSubmitWait(ctx, commandBuffer);
  const auto end = std::chrono::steady_clock::now();
  ctx.freeDescriptorSets(std::vector<VkDescriptorSet>{set});
  if (!submitted) {
    ctx.destroyBuffer(particleBuffer);
    ctx.destroyBuffer(uniformBuffer);
    ctx.destroyBuffer(labelBuffer);
    ctx.destroyBuffer(rgbBuffer);
    ctx.destroyBuffer(alphaBuffer);
    ctx.destroyBuffer(tfBuffer);
    ctx.destroyBuffer(colorMapBuffer);
    return false;
  }

  output.rgb.resize(pixelCount * 3);
  output.weights.resize(pixelCount);
  const bool readRgb = ctx.readBuffer(rgbBuffer,
                                      output.rgb.data(),
                                      rgbBytes);
  const bool readAlpha = ctx.readBuffer(alphaBuffer,
                                        output.weights.data(),
                                        alphaBytes);
  output.elapsedMs =
    std::chrono::duration<double, std::milli>(end - start).count();

  std::size_t nonzeroAlpha = 0;
  double alphaSum = 0.0;
  for (float alpha : output.weights) {
    if (alpha > 0.0f) {
      ++nonzeroAlpha;
      alphaSum += alpha;
    }
  }
  std::cout << "Vulkan Voronoi opacity render: particles="
            << input.particles.size()
            << " grid=" << input.width << "x" << input.height << "x"
            << input.depth
            << " nonzeroAlpha=" << nonzeroAlpha
            << " alphaSum=" << alphaSum
            << " elapsedMs=" << output.elapsedMs
            << std::endl;

  ctx.destroyBuffer(particleBuffer);
  ctx.destroyBuffer(uniformBuffer);
  ctx.destroyBuffer(labelBuffer);
  ctx.destroyBuffer(rgbBuffer);
  ctx.destroyBuffer(alphaBuffer);
  ctx.destroyBuffer(tfBuffer);
  ctx.destroyBuffer(colorMapBuffer);
  ctx.resetReusableProjectionResources();
  return readRgb && readAlpha;
#endif
}

bool RunVulkanVoronoiProjectionMap(const ProjectionGpuMapInput& input,
                                   ProjectionGpuMapOutput& output)
{
  ProjectionGpuLabelGrid grid;
  if (!BuildVulkanVoronoiLabelGrid(input, grid)) {
    return false;
  }
  if (!IntegrateVulkanVoronoiLabelGrid(input, grid, output)) {
    return false;
  }
  output.elapsedMs += grid.elapsedMs;
  return true;
}
