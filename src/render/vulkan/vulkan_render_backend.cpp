#include "render/render_backend.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "projection/projection_map_ui_state.h"

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

} // namespace

class VulkanRenderBackend final : public RenderBackend {
public:
  void init() override
  {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "particle_vis";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "particle_vis";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
    if (result != VK_SUCCESS) {
      std::cerr << "Failed to initialize Vulkan backend: "
                << VkResultName(result) << std::endl;
      instance_ = VK_NULL_HANDLE;
      return;
    }

    std::uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    if (deviceCount > 0) {
      vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());
    }

    std::cerr << "Vulkan backend initialized; physical devices: "
              << deviceCount << std::endl;
    for (VkPhysicalDevice device : devices) {
      VkPhysicalDeviceProperties props{};
      vkGetPhysicalDeviceProperties(device, &props);
      std::cerr << "  Vulkan device: " << props.deviceName << std::endl;
    }
  }

  void destroy() override
  {
    if (instance_ != VK_NULL_HANDLE) {
      vkDestroyInstance(instance_, nullptr);
      instance_ = VK_NULL_HANDLE;
    }
  }

  void render(const RenderFrameState&, const RenderSceneData&) override {}

  void updateProjectionPreview(const RgbImage&) override {
    preview_ = ProjectionPreviewUIState{};
  }

  ProjectionPreviewUIState makeProjectionPreviewUIState() const override {
    return preview_;
  }

  RenderBackendCapabilities capabilities() const override { return {}; }

private:
  VkInstance instance_ = VK_NULL_HANDLE;
  ProjectionPreviewUIState preview_;
};

std::unique_ptr<RenderBackend> CreateVulkanRenderBackend()
{
  return std::make_unique<VulkanRenderBackend>();
}
