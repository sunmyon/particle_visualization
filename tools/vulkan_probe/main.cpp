#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
      return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return "VK_RESULT_UNKNOWN";
  }
}

void Check(VkResult result, const char* what)
{
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(what) + " failed: " +
                             VkResultName(result));
  }
}

void GlfwErrorCallback(int error, const char* description)
{
  std::cerr << "GLFW error " << error << ": "
            << (description ? description : "(no description)") << std::endl;
}

std::vector<const char*> EnumerateInstanceExtensions()
{
  std::uint32_t glfwCount = 0;
  const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
  if (!glfwExtensions || glfwCount == 0) {
    throw std::runtime_error("GLFW did not report Vulkan instance extensions");
  }

  std::vector<const char*> extensions(glfwExtensions,
                                      glfwExtensions + glfwCount);

  std::uint32_t availableCount = 0;
  Check(vkEnumerateInstanceExtensionProperties(nullptr,
                                               &availableCount,
                                               nullptr),
        "vkEnumerateInstanceExtensionProperties");
  std::vector<VkExtensionProperties> available(availableCount);
  Check(vkEnumerateInstanceExtensionProperties(nullptr,
                                               &availableCount,
                                               available.data()),
        "vkEnumerateInstanceExtensionProperties");

  auto hasExtension = [&](const char* name) {
    return std::any_of(available.begin(),
                       available.end(),
                       [&](const VkExtensionProperties& prop) {
                         return std::strcmp(prop.extensionName, name) == 0;
                       });
  };

#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
  if (hasExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
  }
#endif

  return extensions;
}

VkInstance CreateInstance()
{
  const std::vector<const char*> extensions = EnumerateInstanceExtensions();
  const bool enablePortabilityEnumeration =
    std::any_of(extensions.begin(),
                extensions.end(),
                [](const char* extension) {
                  return std::strcmp(extension,
                                     VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0;
                });

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "particle_vis vulkan_probe";
  appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.pEngineName = "particle_vis";
  appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount =
    static_cast<std::uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();
  if (enablePortabilityEnumeration) {
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }

  VkInstance instance = VK_NULL_HANDLE;
  Check(vkCreateInstance(&createInfo, nullptr, &instance), "vkCreateInstance");

  std::cerr << "Vulkan instance extensions:" << std::endl;
  for (const char* ext : extensions) {
    std::cerr << "  " << ext << std::endl;
  }
  return instance;
}

struct QueueFamilies {
  std::optional<std::uint32_t> graphics;
  std::optional<std::uint32_t> present;

  bool complete() const { return graphics && present; }
};

QueueFamilies FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
{
  std::uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

  QueueFamilies result;
  for (std::uint32_t i = 0; i < count; ++i) {
    if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      result.graphics = i;
    }

    VkBool32 presentSupported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupported);
    if (presentSupported) {
      result.present = i;
    }

    if (result.complete()) {
      break;
    }
  }
  return result;
}

bool HasDeviceExtension(VkPhysicalDevice device, const char* name)
{
  std::uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateDeviceExtensionProperties(device,
                                       nullptr,
                                       &count,
                                       extensions.data());
  return std::any_of(extensions.begin(),
                     extensions.end(),
                     [&](const VkExtensionProperties& prop) {
                       return std::strcmp(prop.extensionName, name) == 0;
                     });
}

VkPhysicalDevice PickPhysicalDevice(VkInstance instance,
                                    VkSurfaceKHR surface,
                                    QueueFamilies& queuesOut)
{
  std::uint32_t count = 0;
  Check(vkEnumeratePhysicalDevices(instance, &count, nullptr),
        "vkEnumeratePhysicalDevices");
  if (count == 0) {
    throw std::runtime_error("No Vulkan physical devices found");
  }

  std::vector<VkPhysicalDevice> devices(count);
  Check(vkEnumeratePhysicalDevices(instance, &count, devices.data()),
        "vkEnumeratePhysicalDevices");

  std::cerr << "Vulkan physical devices: " << count << std::endl;
  for (VkPhysicalDevice device : devices) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);
    std::cerr << "  " << props.deviceName << std::endl;

    QueueFamilies queues = FindQueueFamilies(device, surface);
    const bool hasSwapchain =
      HasDeviceExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (queues.complete() && hasSwapchain) {
      queuesOut = queues;
      return device;
    }
  }

  throw std::runtime_error(
    "No Vulkan device supports graphics, present, and swapchain");
}

VkDevice CreateDevice(VkPhysicalDevice physicalDevice,
                      const QueueFamilies& queues,
                      VkQueue& graphicsQueue,
                      VkQueue& presentQueue)
{
  const float priority = 1.0f;
  std::vector<std::uint32_t> uniqueFamilies = { *queues.graphics };
  if (*queues.present != *queues.graphics) {
    uniqueFamilies.push_back(*queues.present);
  }

  std::vector<VkDeviceQueueCreateInfo> queueInfos;
  for (std::uint32_t family : uniqueFamilies) {
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    queueInfos.push_back(queueInfo);
  }

  std::vector<const char*> extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
  if (HasDeviceExtension(physicalDevice,
                         VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
    extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
  }
#endif

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount =
    static_cast<std::uint32_t>(queueInfos.size());
  createInfo.pQueueCreateInfos = queueInfos.data();
  createInfo.enabledExtensionCount =
    static_cast<std::uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  VkDevice device = VK_NULL_HANDLE;
  Check(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device),
        "vkCreateDevice");

  vkGetDeviceQueue(device, *queues.graphics, 0, &graphicsQueue);
  vkGetDeviceQueue(device, *queues.present, 0, &presentQueue);

  std::cerr << "Vulkan device extensions:" << std::endl;
  for (const char* ext : extensions) {
    std::cerr << "  " << ext << std::endl;
  }

  return device;
}

VkSurfaceFormatKHR ChooseSurfaceFormat(
  const std::vector<VkSurfaceFormatKHR>& formats)
{
  for (const VkSurfaceFormatKHR& format : formats) {
    if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return format;
    }
  }
  return formats.front();
}

VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& caps,
                        GLFWwindow* window)
{
  if (caps.currentExtent.width != UINT32_MAX) {
    return caps.currentExtent;
  }

  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  VkExtent2D extent{ static_cast<std::uint32_t>(std::max(width, 1)),
                     static_cast<std::uint32_t>(std::max(height, 1)) };
  extent.width = std::clamp(extent.width,
                            caps.minImageExtent.width,
                            caps.maxImageExtent.width);
  extent.height = std::clamp(extent.height,
                             caps.minImageExtent.height,
                             caps.maxImageExtent.height);
  return extent;
}

struct Swapchain {
  VkSwapchainKHR handle = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  std::vector<VkImage> images;
  std::vector<VkImageLayout> layouts;
};

Swapchain CreateSwapchain(VkPhysicalDevice physicalDevice,
                          VkDevice device,
                          VkSurfaceKHR surface,
                          GLFWwindow* window,
                          const QueueFamilies& queues)
{
  VkSurfaceCapabilitiesKHR caps{};
  Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice,
                                                  surface,
                                                  &caps),
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

  if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
    throw std::runtime_error(
      "Swapchain images do not support transfer-dst clear");
  }

  std::uint32_t formatCount = 0;
  Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice,
                                             surface,
                                             &formatCount,
                                             nullptr),
        "vkGetPhysicalDeviceSurfaceFormatsKHR");
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice,
                                             surface,
                                             &formatCount,
                                             formats.data()),
        "vkGetPhysicalDeviceSurfaceFormatsKHR");

  VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);

  std::uint32_t imageCount = caps.minImageCount + 1;
  if (caps.maxImageCount > 0) {
    imageCount = std::min(imageCount, caps.maxImageCount);
  }

  const VkExtent2D extent = ChooseExtent(caps, window);

  std::array<std::uint32_t, 2> queueFamilyIndices = { *queues.graphics,
                                                     *queues.present };

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = surface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (*queues.graphics != *queues.present) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount =
      static_cast<std::uint32_t>(queueFamilyIndices.size());
    createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }
  createInfo.preTransform = caps.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  createInfo.clipped = VK_TRUE;

  Swapchain swapchain;
  swapchain.format = surfaceFormat.format;
  swapchain.extent = extent;
  Check(vkCreateSwapchainKHR(device,
                             &createInfo,
                             nullptr,
                             &swapchain.handle),
        "vkCreateSwapchainKHR");

  std::uint32_t actualImageCount = 0;
  Check(vkGetSwapchainImagesKHR(device,
                                swapchain.handle,
                                &actualImageCount,
                                nullptr),
        "vkGetSwapchainImagesKHR");
  swapchain.images.resize(actualImageCount);
  Check(vkGetSwapchainImagesKHR(device,
                                swapchain.handle,
                                &actualImageCount,
                                swapchain.images.data()),
        "vkGetSwapchainImagesKHR");
  swapchain.layouts.assign(actualImageCount, VK_IMAGE_LAYOUT_UNDEFINED);

  std::cerr << "Vulkan swapchain: images=" << actualImageCount
            << " extent=" << extent.width << "x" << extent.height
            << " format=" << surfaceFormat.format << std::endl;
  return swapchain;
}

VkCommandPool CreateCommandPool(VkDevice device, std::uint32_t family)
{
  VkCommandPoolCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  createInfo.queueFamilyIndex = family;

  VkCommandPool pool = VK_NULL_HANDLE;
  Check(vkCreateCommandPool(device, &createInfo, nullptr, &pool),
        "vkCreateCommandPool");
  return pool;
}

VkCommandBuffer AllocateCommandBuffer(VkDevice device, VkCommandPool pool)
{
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = pool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  Check(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer),
        "vkAllocateCommandBuffers");
  return commandBuffer;
}

VkSemaphore CreateSemaphore(VkDevice device)
{
  VkSemaphoreCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkSemaphore semaphore = VK_NULL_HANDLE;
  Check(vkCreateSemaphore(device, &createInfo, nullptr, &semaphore),
        "vkCreateSemaphore");
  return semaphore;
}

VkFence CreateFence(VkDevice device)
{
  VkFenceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  VkFence fence = VK_NULL_HANDLE;
  Check(vkCreateFence(device, &createInfo, nullptr, &fence),
        "vkCreateFence");
  return fence;
}

void RecordClearCommand(VkCommandBuffer commandBuffer,
                        VkImage image,
                        VkImageLayout oldLayout,
                        const VkClearColorValue& clearColor)
{
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  Check(vkBeginCommandBuffer(commandBuffer, &beginInfo),
        "vkBeginCommandBuffer");

  VkImageSubresourceRange range{};
  range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  range.baseMipLevel = 0;
  range.levelCount = 1;
  range.baseArrayLayer = 0;
  range.layerCount = 1;

  VkImageMemoryBarrier toTransfer{};
  toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toTransfer.oldLayout = oldLayout;
  toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.image = image;
  toTransfer.subresourceRange = range;
  toTransfer.srcAccessMask = 0;
  toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  const VkPipelineStageFlags srcStage =
    oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
      ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
      : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  vkCmdPipelineBarrier(commandBuffer,
                       srcStage,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0,
                       0,
                       nullptr,
                       0,
                       nullptr,
                       1,
                       &toTransfer);

  vkCmdClearColorImage(commandBuffer,
                       image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       &clearColor,
                       1,
                       &range);

  VkImageMemoryBarrier toPresent{};
  toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toPresent.image = image;
  toPresent.subresourceRange = range;
  toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toPresent.dstAccessMask = 0;

  vkCmdPipelineBarrier(commandBuffer,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                       0,
                       0,
                       nullptr,
                       0,
                       nullptr,
                       1,
                       &toPresent);

  Check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
}

int ParseFrames(int argc, char** argv)
{
  if (argc >= 2) {
    return std::max(1, std::atoi(argv[1]));
  }
  return 240;
}

} // namespace

int main(int argc, char** argv)
{
  const int maxFrames = ParseFrames(argc, argv);

  glfwSetErrorCallback(GlfwErrorCallback);
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    return 1;
  }
  if (!glfwVulkanSupported()) {
    std::cerr << "GLFW reports Vulkan is not supported" << std::endl;
    glfwTerminate();
    return 1;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window =
    glfwCreateWindow(960, 540, "particle_vis Vulkan probe", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }

  VkInstance instance = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue graphicsQueue = VK_NULL_HANDLE;
  VkQueue presentQueue = VK_NULL_HANDLE;
  Swapchain swapchain;
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkSemaphore imageAvailable = VK_NULL_HANDLE;
  VkSemaphore renderFinished = VK_NULL_HANDLE;
  VkFence inFlight = VK_NULL_HANDLE;
  bool ok = false;

  try {
    instance = CreateInstance();
    Check(glfwCreateWindowSurface(instance, window, nullptr, &surface),
          "glfwCreateWindowSurface");

    QueueFamilies queues;
    physicalDevice = PickPhysicalDevice(instance, surface, queues);
    device = CreateDevice(physicalDevice, queues, graphicsQueue, presentQueue);
    swapchain =
      CreateSwapchain(physicalDevice, device, surface, window, queues);
    commandPool = CreateCommandPool(device, *queues.graphics);
    commandBuffer = AllocateCommandBuffer(device, commandPool);
    imageAvailable = CreateSemaphore(device);
    renderFinished = CreateSemaphore(device);
    inFlight = CreateFence(device);

    const auto start = std::chrono::steady_clock::now();
    int frame = 0;
    while (!glfwWindowShouldClose(window) && frame < maxFrames) {
      glfwPollEvents();

      Check(vkWaitForFences(device, 1, &inFlight, VK_TRUE, UINT64_MAX),
            "vkWaitForFences");
      Check(vkResetFences(device, 1, &inFlight), "vkResetFences");

      std::uint32_t imageIndex = 0;
      VkResult acquireResult = vkAcquireNextImageKHR(device,
                                                     swapchain.handle,
                                                     UINT64_MAX,
                                                     imageAvailable,
                                                     VK_NULL_HANDLE,
                                                     &imageIndex);
      if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        std::cerr << "Swapchain is out of date; resize handling is not part of "
                     "this probe."
                  << std::endl;
        break;
      }
      Check(acquireResult, "vkAcquireNextImageKHR");

      Check(vkResetCommandBuffer(commandBuffer, 0), "vkResetCommandBuffer");
      const float t =
        std::chrono::duration<float>(std::chrono::steady_clock::now() - start)
          .count();
      VkClearColorValue clearColor = { { 0.05f,
                                         0.12f + 0.08f * std::sin(t),
                                         0.22f + 0.08f * std::cos(t),
                                         1.0f } };
      RecordClearCommand(commandBuffer,
                         swapchain.images[imageIndex],
                         swapchain.layouts[imageIndex],
                         clearColor);
      swapchain.layouts[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

      VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_TRANSFER_BIT;
      VkSubmitInfo submitInfo{};
      submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submitInfo.waitSemaphoreCount = 1;
      submitInfo.pWaitSemaphores = &imageAvailable;
      submitInfo.pWaitDstStageMask = &waitStage;
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &commandBuffer;
      submitInfo.signalSemaphoreCount = 1;
      submitInfo.pSignalSemaphores = &renderFinished;
      Check(vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlight),
            "vkQueueSubmit");

      VkPresentInfoKHR presentInfo{};
      presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
      presentInfo.waitSemaphoreCount = 1;
      presentInfo.pWaitSemaphores = &renderFinished;
      presentInfo.swapchainCount = 1;
      presentInfo.pSwapchains = &swapchain.handle;
      presentInfo.pImageIndices = &imageIndex;
      VkResult presentResult = vkQueuePresentKHR(presentQueue, &presentInfo);
      if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
          presentResult == VK_SUBOPTIMAL_KHR) {
        std::cerr << "Swapchain became suboptimal/out-of-date; resize handling "
                     "is not part of this probe."
                  << std::endl;
        break;
      }
      Check(presentResult, "vkQueuePresentKHR");

      ++frame;
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    Check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle");
    std::cerr << "Vulkan probe completed." << std::endl;
    ok = true;
  } catch (const std::exception& e) {
    std::cerr << "Vulkan probe failed: " << e.what() << std::endl;
  }

  if (device != VK_NULL_HANDLE) {
    if (inFlight != VK_NULL_HANDLE) {
      vkDestroyFence(device, inFlight, nullptr);
    }
    if (renderFinished != VK_NULL_HANDLE) {
      vkDestroySemaphore(device, renderFinished, nullptr);
    }
    if (imageAvailable != VK_NULL_HANDLE) {
      vkDestroySemaphore(device, imageAvailable, nullptr);
    }
    if (commandPool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device, commandPool, nullptr);
    }
    if (swapchain.handle != VK_NULL_HANDLE) {
      vkDestroySwapchainKHR(device, swapchain.handle, nullptr);
    }
    vkDestroyDevice(device, nullptr);
  }
  if (surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance, surface, nullptr);
  }
  if (instance != VK_NULL_HANDLE) {
    vkDestroyInstance(instance, nullptr);
  }
  glfwDestroyWindow(window);
  glfwTerminate();

  return ok ? 0 : 1;
}
