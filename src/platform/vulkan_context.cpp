#include "platform/vulkan_context.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include "platform/window_backend.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace {

VkAllocationCallbacks* Allocator()
{
  return nullptr;
}

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
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
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

std::vector<VkExtensionProperties> EnumerateInstanceExtensions()
{
  std::uint32_t count = 0;
  if (!Check(vkEnumerateInstanceExtensionProperties(nullptr,
                                                    &count,
                                                    nullptr),
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

#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
bool HasDeviceExtension(VkPhysicalDevice device, const char* extension)
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
#endif

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

} // namespace

VulkanContext::VulkanContext()
  : windowData_(new ImGui_ImplVulkanH_Window())
{
}

VulkanContext::~VulkanContext()
{
  destroy();
  delete windowData_;
}

void VulkanContext::configureWindowHints() const
{
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#endif
}

bool VulkanContext::initFromWindow(NativeWindowHandle window)
{
#ifdef PARTICLE_VIS_HEADLESS_ONLY
  (void)window;
  return false;
#else
  if (window.backend != NativeWindowBackend::GLFW || !window.handle) {
    return false;
  }
  window_ = static_cast<GLFWwindow*>(window.handle);
  if (!glfwVulkanSupported()) {
    std::cerr << "GLFW reports Vulkan is not supported" << std::endl;
    return false;
  }
  if (!createInstance() || !createDevice() || !createDescriptorPool() ||
      !createSurfaceAndWindow(window_)) {
    destroy();
    return false;
  }
  return true;
#endif
}

bool VulkanContext::initHeadless(int width, int height)
{
  (void)width;
  (void)height;
  std::cerr << "Vulkan headless mode is not implemented." << std::endl;
  return false;
}

void VulkanContext::destroy()
{
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
  }
  cleanupWindow();
  cleanupVulkan();
  window_ = nullptr;
}

void VulkanContext::present(NativeWindowHandle window)
{
  (void)window;
  if (!frameRecorded_ || swapChainRebuild_ || !windowData_) {
    return;
  }

  VkSemaphore renderComplete =
    windowData_->FrameSemaphores[windowData_->SemaphoreIndex]
      .RenderCompleteSemaphore;

  VkPresentInfoKHR info{};
  info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  info.waitSemaphoreCount = 1;
  info.pWaitSemaphores = &renderComplete;
  info.swapchainCount = 1;
  info.pSwapchains = &windowData_->Swapchain;
  info.pImageIndices = &windowData_->FrameIndex;

  const VkResult result = vkQueuePresentKHR(queue_, &info);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    swapChainRebuild_ = true;
    frameRecorded_ = false;
    return;
  }
  Check(result, "vkQueuePresentKHR");

  windowData_->SemaphoreIndex =
    (windowData_->SemaphoreIndex + 1) % windowData_->SemaphoreCount;
  frameRecorded_ = false;
}

RenderedFrame VulkanContext::readDefaultFramebuffer(int width, int height)
{
  (void)width;
  (void)height;
  return {};
}

void VulkanContext::renderImGuiDrawData(ImDrawData* drawData)
{
  frameRecorded_ = false;
  if (!drawData || !windowData_ || device_ == VK_NULL_HANDLE) {
    return;
  }
  if (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f) {
    return;
  }

  resizeIfNeeded();
  if (swapChainRebuild_) {
    return;
  }

  VkSemaphore imageAcquired =
    windowData_->FrameSemaphores[windowData_->SemaphoreIndex]
      .ImageAcquiredSemaphore;
  VkSemaphore renderComplete =
    windowData_->FrameSemaphores[windowData_->SemaphoreIndex]
      .RenderCompleteSemaphore;

  VkResult result = vkAcquireNextImageKHR(device_,
                                          windowData_->Swapchain,
                                          UINT64_MAX,
                                          imageAcquired,
                                          VK_NULL_HANDLE,
                                          &windowData_->FrameIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    swapChainRebuild_ = true;
    return;
  }
  if (!Check(result, "vkAcquireNextImageKHR")) {
    return;
  }

  ImGui_ImplVulkanH_Frame* frame =
    &windowData_->Frames[windowData_->FrameIndex];
  if (!Check(vkWaitForFences(device_,
                             1,
                             &frame->Fence,
                             VK_TRUE,
                             UINT64_MAX),
             "vkWaitForFences")) {
    return;
  }
  if (!Check(vkResetFences(device_, 1, &frame->Fence), "vkResetFences")) {
    return;
  }
  if (!Check(vkResetCommandPool(device_, frame->CommandPool, 0),
             "vkResetCommandPool")) {
    return;
  }

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (!Check(vkBeginCommandBuffer(frame->CommandBuffer, &beginInfo),
             "vkBeginCommandBuffer")) {
    return;
  }

  if (preRender_) {
    preRender_(frame->CommandBuffer);
  }

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = windowData_->RenderPass;
  renderPassInfo.framebuffer = frame->Framebuffer;
  renderPassInfo.renderArea.extent.width = windowData_->Width;
  renderPassInfo.renderArea.extent.height = windowData_->Height;
  std::array<VkClearValue, 2> clearValues{};
  clearValues[0] = windowData_->ClearValue;
  clearValues[1].depthStencil.depth = 1.0f;
  clearValues[1].depthStencil.stencil = 0;
  renderPassInfo.clearValueCount =
    static_cast<std::uint32_t>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();
  vkCmdBeginRenderPass(frame->CommandBuffer,
                       &renderPassInfo,
                       VK_SUBPASS_CONTENTS_INLINE);

  if (preImGuiDraw_) {
    preImGuiDraw_(frame->CommandBuffer);
  }

  ImGui_ImplVulkan_RenderDrawData(drawData, frame->CommandBuffer);
  vkCmdEndRenderPass(frame->CommandBuffer);

  if (!Check(vkEndCommandBuffer(frame->CommandBuffer),
             "vkEndCommandBuffer")) {
    return;
  }

  VkPipelineStageFlags waitStage =
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &imageAcquired;
  submitInfo.pWaitDstStageMask = &waitStage;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &frame->CommandBuffer;
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &renderComplete;

  if (!Check(vkQueueSubmit(queue_, 1, &submitInfo, frame->Fence),
             "vkQueueSubmit")) {
    return;
  }

  frameRecorded_ = true;
}

VkRenderPass VulkanContext::renderPass() const
{
  return windowData_ ? windowData_->RenderPass : VK_NULL_HANDLE;
}

std::uint32_t VulkanContext::imageCount() const
{
  return windowData_ ? windowData_->ImageCount : 0;
}

VkExtent2D VulkanContext::swapchainExtent() const
{
  if (!windowData_) {
    return {};
  }
  return VkExtent2D{ static_cast<std::uint32_t>(windowData_->Width),
                     static_cast<std::uint32_t>(windowData_->Height) };
}

void VulkanContext::setPreImGuiDrawCallback(PreImGuiDrawCallback callback)
{
  preImGuiDraw_ = std::move(callback);
}

void VulkanContext::setPreRenderCallback(PreRenderCallback callback)
{
  preRender_ = std::move(callback);
}

bool VulkanContext::createInstance()
{
  std::uint32_t glfwCount = 0;
  const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
  if (!glfwExtensions || glfwCount == 0) {
    std::cerr << "GLFW did not report Vulkan instance extensions" << std::endl;
    return false;
  }

  std::vector<const char*> extensions(glfwExtensions,
                                      glfwExtensions + glfwCount);
  const std::vector<VkExtensionProperties> available =
    EnumerateInstanceExtensions();

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  if (IsExtensionAvailable(available,
                           VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
  }
  if (IsExtensionAvailable(available,
                           VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "particle_vis";
  appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.pEngineName = "particle_vis";
  appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.apiVersion = VK_API_VERSION_1_1;

  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount =
    static_cast<std::uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  if (!Check(vkCreateInstance(&createInfo, Allocator(), &instance_),
             "vkCreateInstance")) {
    return false;
  }
  return true;
}

bool VulkanContext::createDevice()
{
  physicalDevice_ = ImGui_ImplVulkanH_SelectPhysicalDevice(instance_);
  if (physicalDevice_ == VK_NULL_HANDLE) {
    std::cerr << "No Vulkan physical device found" << std::endl;
    return false;
  }

  queueFamily_ = ImGui_ImplVulkanH_SelectQueueFamilyIndex(physicalDevice_);
  if (queueFamily_ == UINT32_MAX) {
    std::cerr << "No Vulkan graphics queue family found" << std::endl;
    return false;
  }

  std::vector<const char*> extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
  if (HasDeviceExtension(physicalDevice_,
                         VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
    extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
  }
#endif

  const float queuePriority = 1.0f;
  VkDeviceQueueCreateInfo queueInfo{};
  queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueInfo.queueFamilyIndex = queueFamily_;
  queueInfo.queueCount = 1;
  queueInfo.pQueuePriorities = &queuePriority;

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount = 1;
  createInfo.pQueueCreateInfos = &queueInfo;
  createInfo.enabledExtensionCount =
    static_cast<std::uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  if (!Check(vkCreateDevice(physicalDevice_,
                            &createInfo,
                            Allocator(),
                            &device_),
             "vkCreateDevice")) {
    return false;
  }
  vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

  VkPhysicalDeviceProperties props{};
  vkGetPhysicalDeviceProperties(physicalDevice_, &props);
  std::cerr << "Vulkan platform device: " << props.deviceName << std::endl;
  return true;
}

bool VulkanContext::createDescriptorPool()
{
  // ImGui's Vulkan backend consumes one combined image sampler for the font
  // atlas. Projection preview registers at most one additional live texture:
  // the preview descriptor is reused for same-size updates and removed before
  // reallocating after a size change. Keep a large spare margin for future
  // ImGui textures because ImGui_ImplVulkan_AddTexture() does not expose a
  // recoverable allocation status in this bundled backend.
  constexpr std::uint32_t kImGuiCombinedImageSamplerDescriptors = 64;
  VkDescriptorPoolSize poolSizes[] = {
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      kImGuiCombinedImageSamplerDescriptors },
  };

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolInfo.maxSets = kImGuiCombinedImageSamplerDescriptors;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = poolSizes;

  return Check(vkCreateDescriptorPool(device_,
                                      &poolInfo,
                                      Allocator(),
                                      &descriptorPool_),
               "vkCreateDescriptorPool");
}

bool VulkanContext::createSurfaceAndWindow(GLFWwindow* window)
{
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!Check(glfwCreateWindowSurface(instance_,
                                     window,
                                     Allocator(),
                                     &surface),
             "glfwCreateWindowSurface")) {
    return false;
  }

  windowData_->Surface = surface;
  VkBool32 presentSupported = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_,
                                       queueFamily_,
                                       windowData_->Surface,
                                       &presentSupported);
  if (presentSupported != VK_TRUE) {
    std::cerr << "Vulkan physical device does not support this window surface"
              << std::endl;
    return false;
  }

  const VkFormat requestFormats[] = {
    VK_FORMAT_B8G8R8A8_UNORM,
    VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_B8G8R8_UNORM,
    VK_FORMAT_R8G8B8_UNORM
  };
  windowData_->SurfaceFormat =
    ImGui_ImplVulkanH_SelectSurfaceFormat(physicalDevice_,
                                          windowData_->Surface,
                                          requestFormats,
                                          4,
                                          VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
  const VkPresentModeKHR presentModes[] = { VK_PRESENT_MODE_FIFO_KHR };
  windowData_->PresentMode =
    ImGui_ImplVulkanH_SelectPresentMode(physicalDevice_,
                                        windowData_->Surface,
                                        presentModes,
                                        1);
  windowData_->ClearValue.color.float32[0] = 0.02f;
  windowData_->ClearValue.color.float32[1] = 0.025f;
  windowData_->ClearValue.color.float32[2] = 0.03f;
  windowData_->ClearValue.color.float32[3] = 1.0f;

  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  ImGui_ImplVulkanH_CreateOrResizeWindow(instance_,
                                         physicalDevice_,
                                         device_,
                                         windowData_,
                                         queueFamily_,
                                         Allocator(),
                                         width,
                                         height,
                                         minImageCount_);
  return rebuildRenderPassWithDepth();
}

void VulkanContext::resizeIfNeeded()
{
  if (!window_ || !windowData_) {
    return;
  }

  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window_, &width, &height);
  if (width <= 0 || height <= 0) {
    return;
  }

  if (!swapChainRebuild_ && windowData_->Width == width &&
      windowData_->Height == height) {
    return;
  }

  ImGui_ImplVulkan_SetMinImageCount(minImageCount_);
  cleanupDepthResources();
  ImGui_ImplVulkanH_CreateOrResizeWindow(instance_,
                                         physicalDevice_,
                                         device_,
                                         windowData_,
                                         queueFamily_,
                                         Allocator(),
                                         width,
                                         height,
                                         minImageCount_);
  rebuildRenderPassWithDepth();
  windowData_->FrameIndex = 0;
  swapChainRebuild_ = false;
}

bool VulkanContext::rebuildRenderPassWithDepth()
{
  if (!windowData_ || device_ == VK_NULL_HANDLE ||
      windowData_->ImageCount == 0) {
    return false;
  }

  for (std::uint32_t i = 0; i < windowData_->ImageCount; ++i) {
    ImGui_ImplVulkanH_Frame& frame = windowData_->Frames[i];
    if (frame.Framebuffer != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(device_, frame.Framebuffer, Allocator());
      frame.Framebuffer = VK_NULL_HANDLE;
    }
  }
  if (windowData_->RenderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device_, windowData_->RenderPass, Allocator());
    windowData_->RenderPass = VK_NULL_HANDLE;
  }

  cleanupDepthResources();
  depthImages_.resize(windowData_->ImageCount, VK_NULL_HANDLE);
  depthMemories_.resize(windowData_->ImageCount, VK_NULL_HANDLE);
  depthImageViews_.resize(windowData_->ImageCount, VK_NULL_HANDLE);

  for (std::uint32_t i = 0; i < windowData_->ImageCount; ++i) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<std::uint32_t>(windowData_->Width);
    imageInfo.extent.height = static_cast<std::uint32_t>(windowData_->Height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (!Check(vkCreateImage(device_,
                             &imageInfo,
                             Allocator(),
                             &depthImages_[i]),
               "vkCreateImage(depth)")) {
      cleanupDepthResources();
      return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, depthImages_[i], &requirements);
    const std::uint32_t memoryType =
      FindMemoryType(physicalDevice_,
                     requirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == UINT32_MAX) {
      std::cerr << "No Vulkan memory type for depth image." << std::endl;
      cleanupDepthResources();
      return false;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = memoryType;
    if (!Check(vkAllocateMemory(device_,
                                &allocInfo,
                                Allocator(),
                                &depthMemories_[i]),
               "vkAllocateMemory(depth)")) {
      cleanupDepthResources();
      return false;
    }
    Check(vkBindImageMemory(device_,
                            depthImages_[i],
                            depthMemories_[i],
                            0),
          "vkBindImageMemory(depth)");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImages_[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (!Check(vkCreateImageView(device_,
                                 &viewInfo,
                                 Allocator(),
                                 &depthImageViews_[i]),
               "vkCreateImageView(depth)")) {
      cleanupDepthResources();
      return false;
    }
  }

  std::array<VkAttachmentDescription, 2> attachments{};
  attachments[0].format = windowData_->SurfaceFormat.format;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp =
    windowData_->ClearEnable ? VK_ATTACHMENT_LOAD_OP_CLEAR
                             : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  attachments[1].format = depthFormat_;
  attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[1].finalLayout =
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorAttachment{};
  colorAttachment.attachment = 0;
  colorAttachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthAttachment{};
  depthAttachment.attachment = 1;
  depthAttachment.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachment;
  subpass.pDepthStencilAttachment = &depthAttachment;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount =
    static_cast<std::uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &dependency;

  if (!Check(vkCreateRenderPass(device_,
                                &renderPassInfo,
                                Allocator(),
                                &windowData_->RenderPass),
             "vkCreateRenderPass(depth)")) {
    cleanupDepthResources();
    return false;
  }

  std::array<VkClearValue, 2> clearValues{};
  clearValues[0] = windowData_->ClearValue;
  clearValues[1].depthStencil.depth = 1.0f;
  clearValues[1].depthStencil.stencil = 0;

  for (std::uint32_t i = 0; i < windowData_->ImageCount; ++i) {
    ImGui_ImplVulkanH_Frame& frame = windowData_->Frames[i];
    std::array<VkImageView, 2> framebufferAttachments = {
      frame.BackbufferView,
      depthImageViews_[i]
    };

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = windowData_->RenderPass;
    framebufferInfo.attachmentCount =
      static_cast<std::uint32_t>(framebufferAttachments.size());
    framebufferInfo.pAttachments = framebufferAttachments.data();
    framebufferInfo.width = windowData_->Width;
    framebufferInfo.height = windowData_->Height;
    framebufferInfo.layers = 1;
    if (!Check(vkCreateFramebuffer(device_,
                                   &framebufferInfo,
                                   Allocator(),
                                   &frame.Framebuffer),
               "vkCreateFramebuffer(depth)")) {
      cleanupDepthResources();
      return false;
    }
  }
  return true;
}

void VulkanContext::cleanupDepthResources()
{
  if (device_ == VK_NULL_HANDLE) {
    depthImages_.clear();
    depthMemories_.clear();
    depthImageViews_.clear();
    return;
  }

  for (VkImageView view : depthImageViews_) {
    if (view != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, view, Allocator());
    }
  }
  for (VkImage image : depthImages_) {
    if (image != VK_NULL_HANDLE) {
      vkDestroyImage(device_, image, Allocator());
    }
  }
  for (VkDeviceMemory memory : depthMemories_) {
    if (memory != VK_NULL_HANDLE) {
      vkFreeMemory(device_, memory, Allocator());
    }
  }
  depthImages_.clear();
  depthMemories_.clear();
  depthImageViews_.clear();
}

void VulkanContext::cleanupWindow()
{
  if (device_ != VK_NULL_HANDLE && windowData_) {
    cleanupDepthResources();
    ImGui_ImplVulkanH_DestroyWindow(instance_,
                                    device_,
                                    windowData_,
                                    Allocator());
    swapChainRebuild_ = false;
    frameRecorded_ = false;
  }
}

void VulkanContext::cleanupVulkan()
{
  if (device_ != VK_NULL_HANDLE) {
    if (descriptorPool_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_, descriptorPool_, Allocator());
      descriptorPool_ = VK_NULL_HANDLE;
    }
    vkDestroyDevice(device_, Allocator());
    device_ = VK_NULL_HANDLE;
  }

  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, Allocator());
    instance_ = VK_NULL_HANDLE;
  }
  physicalDevice_ = VK_NULL_HANDLE;
  queueFamily_ = UINT32_MAX;
  queue_ = VK_NULL_HANDLE;
}

std::unique_ptr<GraphicsContext> CreateVulkanGraphicsContext()
{
  return std::make_unique<VulkanContext>();
}
