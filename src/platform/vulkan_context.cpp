#include "platform/vulkan_context.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include "platform/window_backend.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

#ifdef __APPLE__
#include <dlfcn.h>
#endif

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

void AppendExtensionIfAvailable(std::vector<const char*>& extensions,
                                const std::vector<VkExtensionProperties>& available,
                                const char* extension)
{
  if (!IsExtensionAvailable(available, extension)) {
    return;
  }
  const bool alreadyAdded =
    std::any_of(extensions.begin(),
                extensions.end(),
                [&](const char* existing) {
                  return std::strcmp(existing, extension) == 0;
                });
  if (!alreadyAdded) {
    extensions.push_back(extension);
  }
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

#ifdef __APPLE__
void ConfigureMoltenVkIcdPathForHeadless()
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

void* OpenMoltenVkLibraryForHeadless()
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
  headless_ = false;
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
  if (width <= 0 || height <= 0) {
    return false;
  }
  headless_ = true;
  window_ = nullptr;
#ifdef __APPLE__
  ConfigureMoltenVkIcdPathForHeadless();
#endif
  if (!createInstance() || !createDevice() || !createDescriptorPool() ||
      !createHeadlessTarget(width, height)) {
    destroy();
    return false;
  }
  return true;
}

void VulkanContext::destroy()
{
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
  }
  cleanupHeadlessResources();
  cleanupWindow();
  cleanupVulkan();
  window_ = nullptr;
  headless_ = false;
}

void VulkanContext::present(NativeWindowHandle window)
{
  (void)window;
  if (headless_) {
    frameRecorded_ = false;
    return;
  }
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
  if (headless_) {
    return readHeadlessFramebuffer(width, height);
  }

  RenderedFrame out;
  out.width = width;
  out.height = height;
  if (!frameRecorded_ || !windowData_ || device_ == VK_NULL_HANDLE ||
      queue_ == VK_NULL_HANDLE || width <= 0 || height <= 0 ||
      windowData_->FrameIndex >= windowData_->ImageCount) {
    return out;
  }

  ImGui_ImplVulkanH_Frame& frame = windowData_->Frames[windowData_->FrameIndex];
  if (frame.Backbuffer == VK_NULL_HANDLE) {
    return out;
  }

  vkWaitForFences(device_, 1, &frame.Fence, VK_TRUE, UINT64_MAX);

  const VkDeviceSize bytes =
    static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;

  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = bytes;
  bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (!Check(vkCreateBuffer(device_,
                            &bufferInfo,
                            Allocator(),
                            &stagingBuffer),
             "vkCreateBuffer(readback)")) {
    return out;
  }

  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device_, stagingBuffer, &requirements);
  const std::uint32_t memoryType =
    FindMemoryType(physicalDevice_,
                   requirements.memoryTypeBits,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (memoryType == UINT32_MAX) {
    vkDestroyBuffer(device_, stagingBuffer, Allocator());
    return out;
  }

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = requirements.size;
  allocInfo.memoryTypeIndex = memoryType;
  if (!Check(vkAllocateMemory(device_,
                              &allocInfo,
                              Allocator(),
                              &stagingMemory),
             "vkAllocateMemory(readback)")) {
    vkDestroyBuffer(device_, stagingBuffer, Allocator());
    return out;
  }
  vkBindBufferMemory(device_, stagingBuffer, stagingMemory, 0);

  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = queueFamily_;
  if (!Check(vkCreateCommandPool(device_,
                                 &poolInfo,
                                 Allocator(),
                                 &commandPool),
             "vkCreateCommandPool(readback)")) {
    vkFreeMemory(device_, stagingMemory, Allocator());
    vkDestroyBuffer(device_, stagingBuffer, Allocator());
    return out;
  }

  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo cmdAlloc{};
  cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdAlloc.commandPool = commandPool;
  cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdAlloc.commandBufferCount = 1;
  vkAllocateCommandBuffers(device_, &cmdAlloc, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  VkImageMemoryBarrier toTransfer{};
  toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.image = frame.Backbuffer;
  toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  toTransfer.subresourceRange.levelCount = 1;
  toTransfer.subresourceRange.layerCount = 1;
  toTransfer.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  vkCmdPipelineBarrier(commandBuffer,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0,
                       0,
                       nullptr,
                       0,
                       nullptr,
                       1,
                       &toTransfer);

  VkBufferImageCopy copy{};
  copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy.imageSubresource.layerCount = 1;
  copy.imageExtent = {static_cast<std::uint32_t>(width),
                      static_cast<std::uint32_t>(height),
                      1};
  vkCmdCopyImageToBuffer(commandBuffer,
                         frame.Backbuffer,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         stagingBuffer,
                         1,
                         &copy);

  VkImageMemoryBarrier toPresent = toTransfer;
  toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  toPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
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

  vkEndCommandBuffer(commandBuffer);
  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  const bool submitted =
    Check(vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE),
          "vkQueueSubmit(readback)");
  if (submitted) {
    vkQueueWaitIdle(queue_);
    out.format = RenderedFrameFormat::RGBA8;
    out.pixels.resize(static_cast<std::size_t>(bytes));
    void* mapped = nullptr;
    if (Check(vkMapMemory(device_, stagingMemory, 0, bytes, 0, &mapped),
              "vkMapMemory(readback)")) {
      const auto* src = static_cast<const std::uint8_t*>(mapped);
      const bool bgra =
        windowData_->SurfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM ||
        windowData_->SurfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB;
      for (std::size_t i = 0; i < out.pixels.size(); i += 4) {
        if (bgra) {
          out.pixels[i + 0] = src[i + 2];
          out.pixels[i + 1] = src[i + 1];
          out.pixels[i + 2] = src[i + 0];
          out.pixels[i + 3] = src[i + 3];
        } else {
          out.pixels[i + 0] = src[i + 0];
          out.pixels[i + 1] = src[i + 1];
          out.pixels[i + 2] = src[i + 2];
          out.pixels[i + 3] = src[i + 3];
        }
      }
      vkUnmapMemory(device_, stagingMemory);
    }
  }

  vkDestroyCommandPool(device_, commandPool, Allocator());
  vkFreeMemory(device_, stagingMemory, Allocator());
  vkDestroyBuffer(device_, stagingBuffer, Allocator());
  return out;
}

void VulkanContext::renderImGuiDrawData(ImDrawData* drawData)
{
  frameRecorded_ = false;
  if (!drawData || device_ == VK_NULL_HANDLE) {
    return;
  }
  if (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f) {
    return;
  }

  if (headless_) {
    if (headlessCommandBuffer_ == VK_NULL_HANDLE ||
        headlessFramebuffer_ == VK_NULL_HANDLE ||
        headlessRenderPass_ == VK_NULL_HANDLE) {
      return;
    }
    if (!Check(vkWaitForFences(device_,
                               1,
                               &headlessFence_,
                               VK_TRUE,
                               UINT64_MAX),
               "vkWaitForFences(headless)")) {
      return;
    }
    if (!Check(vkResetFences(device_, 1, &headlessFence_),
               "vkResetFences(headless)")) {
      return;
    }
    if (!Check(vkResetCommandPool(device_, headlessCommandPool_, 0),
               "vkResetCommandPool(headless)")) {
      return;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!Check(vkBeginCommandBuffer(headlessCommandBuffer_, &beginInfo),
               "vkBeginCommandBuffer(headless)")) {
      return;
    }

    if (preRender_) {
      preRender_(headlessCommandBuffer_);
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = headlessRenderPass_;
    renderPassInfo.framebuffer = headlessFramebuffer_;
    renderPassInfo.renderArea.extent.width =
      static_cast<std::uint32_t>(headlessWidth_);
    renderPassInfo.renderArea.extent.height =
      static_cast<std::uint32_t>(headlessHeight_);
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color.float32[0] = 0.02f;
    clearValues[0].color.float32[1] = 0.025f;
    clearValues[0].color.float32[2] = 0.03f;
    clearValues[0].color.float32[3] = 1.0f;
    clearValues[1].depthStencil.depth = 1.0f;
    clearValues[1].depthStencil.stencil = 0;
    renderPassInfo.clearValueCount =
      static_cast<std::uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(headlessCommandBuffer_,
                         &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);

    if (preImGuiDraw_) {
      preImGuiDraw_(headlessCommandBuffer_);
    }

    ImGui_ImplVulkan_RenderDrawData(drawData, headlessCommandBuffer_);
    vkCmdEndRenderPass(headlessCommandBuffer_);

    if (!Check(vkEndCommandBuffer(headlessCommandBuffer_),
               "vkEndCommandBuffer(headless)")) {
      return;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &headlessCommandBuffer_;
    if (!Check(vkQueueSubmit(queue_, 1, &submitInfo, headlessFence_),
               "vkQueueSubmit(headless)")) {
      return;
    }
    frameRecorded_ = true;
    return;
  }

  if (!windowData_) {
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
  if (headless_) {
    return headlessRenderPass_;
  }
  return windowData_ ? windowData_->RenderPass : VK_NULL_HANDLE;
}

std::uint32_t VulkanContext::imageCount() const
{
  if (headless_) {
    return minImageCount_;
  }
  return windowData_ ? windowData_->ImageCount : 0;
}

VkExtent2D VulkanContext::swapchainExtent() const
{
  if (headless_) {
    return VkExtent2D{ static_cast<std::uint32_t>(headlessWidth_),
                       static_cast<std::uint32_t>(headlessHeight_) };
  }
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
  std::vector<const char*> extensions;
  if (!headless_) {
    std::uint32_t glfwCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
    if (!glfwExtensions || glfwCount == 0) {
      std::cerr << "GLFW did not report Vulkan instance extensions"
                << std::endl;
      return false;
    }
    extensions.assign(glfwExtensions, glfwExtensions + glfwCount);
  }
  const std::vector<VkExtensionProperties> available =
    EnumerateInstanceExtensions();

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#if defined(__APPLE__) && defined(VK_LUNARG_DIRECT_DRIVER_LOADING_EXTENSION_NAME)
  VkDirectDriverLoadingInfoLUNARG directDriver{};
  VkDirectDriverLoadingListLUNARG directDriverList{};
  if (headless_ &&
      IsExtensionAvailable(available,
                           VK_LUNARG_DIRECT_DRIVER_LOADING_EXTENSION_NAME)) {
    if (!moltenVkLibrary_) {
      moltenVkLibrary_ = OpenMoltenVkLibraryForHeadless();
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
  if (headless_) {
    AppendExtensionIfAvailable(extensions, available, VK_KHR_SURFACE_EXTENSION_NAME);
    AppendExtensionIfAvailable(extensions, available, "VK_EXT_metal_surface");
  }
  if (IsExtensionAvailable(available,
                           VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
    AppendExtensionIfAvailable(extensions,
                               available,
                               VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
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

  const VkResult result = vkCreateInstance(&createInfo, Allocator(), &instance_);
  if (result != VK_SUCCESS) {
    std::cerr << "vkCreateInstance failed: " << VkResultName(result)
              << " extensions=";
    for (const char* extension : extensions) {
      std::cerr << extension << " ";
    }
    std::cerr << "flags=0x" << std::hex << createInfo.flags << std::dec
              << std::endl;
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

  std::vector<const char*> extensions;
  if (!headless_) {
    extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  }
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
  if (HasDeviceExtension(physicalDevice_,
                         VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
    extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
  }
#endif
#ifdef VK_EXT_MEMORY_BUDGET_EXTENSION_NAME
  if (HasDeviceExtension(physicalDevice_, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)) {
    extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
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

bool VulkanContext::createHeadlessImage(VkFormat format,
                                        VkImageUsageFlags usage,
                                        VkImageAspectFlags aspect,
                                        VkImage& image,
                                        VkDeviceMemory& memory,
                                        VkImageView& view)
{
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = static_cast<std::uint32_t>(headlessWidth_);
  imageInfo.extent.height = static_cast<std::uint32_t>(headlessHeight_);
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (!Check(vkCreateImage(device_, &imageInfo, Allocator(), &image),
             "vkCreateImage(headless)")) {
    return false;
  }

  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(device_, image, &requirements);
  const std::uint32_t memoryType =
    FindMemoryType(physicalDevice_,
                   requirements.memoryTypeBits,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (memoryType == UINT32_MAX) {
    std::cerr << "No Vulkan memory type for headless image." << std::endl;
    return false;
  }

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = requirements.size;
  allocInfo.memoryTypeIndex = memoryType;
  if (!Check(vkAllocateMemory(device_, &allocInfo, Allocator(), &memory),
             "vkAllocateMemory(headless)")) {
    return false;
  }
  if (!Check(vkBindImageMemory(device_, image, memory, 0),
             "vkBindImageMemory(headless)")) {
    return false;
  }

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspect;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;
  return Check(vkCreateImageView(device_, &viewInfo, Allocator(), &view),
               "vkCreateImageView(headless)");
}

bool VulkanContext::createHeadlessRenderPass()
{
  std::array<VkAttachmentDescription, 2> attachments{};
  attachments[0].format = headlessColorFormat_;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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
  return Check(vkCreateRenderPass(device_,
                                  &renderPassInfo,
                                  Allocator(),
                                  &headlessRenderPass_),
               "vkCreateRenderPass(headless)");
}

bool VulkanContext::createHeadlessTarget(int width, int height)
{
  headlessWidth_ = width;
  headlessHeight_ = height;
  minImageCount_ = 2;

  if (!createHeadlessRenderPass()) {
    return false;
  }
  if (!createHeadlessImage(headlessColorFormat_,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT,
                           headlessColorImage_,
                           headlessColorMemory_,
                           headlessColorView_)) {
    return false;
  }
  if (!createHeadlessImage(depthFormat_,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                           VK_IMAGE_ASPECT_DEPTH_BIT,
                           headlessDepthImage_,
                           headlessDepthMemory_,
                           headlessDepthView_)) {
    return false;
  }

  std::array<VkImageView, 2> attachments = {
    headlessColorView_,
    headlessDepthView_
  };
  VkFramebufferCreateInfo framebufferInfo{};
  framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass = headlessRenderPass_;
  framebufferInfo.attachmentCount =
    static_cast<std::uint32_t>(attachments.size());
  framebufferInfo.pAttachments = attachments.data();
  framebufferInfo.width = static_cast<std::uint32_t>(headlessWidth_);
  framebufferInfo.height = static_cast<std::uint32_t>(headlessHeight_);
  framebufferInfo.layers = 1;
  if (!Check(vkCreateFramebuffer(device_,
                                 &framebufferInfo,
                                 Allocator(),
                                 &headlessFramebuffer_),
             "vkCreateFramebuffer(headless)")) {
    return false;
  }

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = queueFamily_;
  if (!Check(vkCreateCommandPool(device_,
                                 &poolInfo,
                                 Allocator(),
                                 &headlessCommandPool_),
             "vkCreateCommandPool(headless)")) {
    return false;
  }

  VkCommandBufferAllocateInfo cmdAlloc{};
  cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdAlloc.commandPool = headlessCommandPool_;
  cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdAlloc.commandBufferCount = 1;
  if (!Check(vkAllocateCommandBuffers(device_,
                                      &cmdAlloc,
                                      &headlessCommandBuffer_),
             "vkAllocateCommandBuffers(headless)")) {
    return false;
  }

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  return Check(vkCreateFence(device_, &fenceInfo, Allocator(), &headlessFence_),
               "vkCreateFence(headless)");
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

RenderedFrame VulkanContext::readHeadlessFramebuffer(int width, int height)
{
  RenderedFrame out;
  out.width = width;
  out.height = height;
  if (!frameRecorded_ || device_ == VK_NULL_HANDLE ||
      queue_ == VK_NULL_HANDLE || headlessColorImage_ == VK_NULL_HANDLE ||
      width <= 0 || height <= 0 || width != headlessWidth_ ||
      height != headlessHeight_) {
    return out;
  }

  vkWaitForFences(device_, 1, &headlessFence_, VK_TRUE, UINT64_MAX);

  const VkDeviceSize bytes =
    static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = bytes;
  bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (!Check(vkCreateBuffer(device_,
                            &bufferInfo,
                            Allocator(),
                            &stagingBuffer),
             "vkCreateBuffer(headless readback)")) {
    return out;
  }

  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device_, stagingBuffer, &requirements);
  const std::uint32_t memoryType =
    FindMemoryType(physicalDevice_,
                   requirements.memoryTypeBits,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (memoryType == UINT32_MAX) {
    vkDestroyBuffer(device_, stagingBuffer, Allocator());
    return out;
  }

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = requirements.size;
  allocInfo.memoryTypeIndex = memoryType;
  if (!Check(vkAllocateMemory(device_,
                              &allocInfo,
                              Allocator(),
                              &stagingMemory),
             "vkAllocateMemory(headless readback)")) {
    vkDestroyBuffer(device_, stagingBuffer, Allocator());
    return out;
  }
  vkBindBufferMemory(device_, stagingBuffer, stagingMemory, 0);

  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = queueFamily_;
  if (!Check(vkCreateCommandPool(device_,
                                 &poolInfo,
                                 Allocator(),
                                 &commandPool),
             "vkCreateCommandPool(headless readback)")) {
    vkFreeMemory(device_, stagingMemory, Allocator());
    vkDestroyBuffer(device_, stagingBuffer, Allocator());
    return out;
  }

  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo cmdAlloc{};
  cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdAlloc.commandPool = commandPool;
  cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdAlloc.commandBufferCount = 1;
  vkAllocateCommandBuffers(device_, &cmdAlloc, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  VkImageMemoryBarrier toTransfer{};
  toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toTransfer.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.image = headlessColorImage_;
  toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  toTransfer.subresourceRange.levelCount = 1;
  toTransfer.subresourceRange.layerCount = 1;
  toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  vkCmdPipelineBarrier(commandBuffer,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0,
                       0,
                       nullptr,
                       0,
                       nullptr,
                       1,
                       &toTransfer);

  VkBufferImageCopy copy{};
  copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy.imageSubresource.layerCount = 1;
  copy.imageExtent = {static_cast<std::uint32_t>(width),
                      static_cast<std::uint32_t>(height),
                      1};
  vkCmdCopyImageToBuffer(commandBuffer,
                         headlessColorImage_,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         stagingBuffer,
                         1,
                         &copy);

  VkImageMemoryBarrier toColor = toTransfer;
  toColor.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  toColor.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  vkCmdPipelineBarrier(commandBuffer,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       0,
                       0,
                       nullptr,
                       0,
                       nullptr,
                       1,
                       &toColor);

  vkEndCommandBuffer(commandBuffer);
  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  const bool submitted =
    Check(vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE),
          "vkQueueSubmit(headless readback)");
  if (submitted) {
    vkQueueWaitIdle(queue_);
    out.format = RenderedFrameFormat::RGBA8;
    out.pixels.resize(static_cast<std::size_t>(bytes));
    void* mapped = nullptr;
    if (Check(vkMapMemory(device_, stagingMemory, 0, bytes, 0, &mapped),
              "vkMapMemory(headless readback)")) {
      const auto* src = static_cast<const std::uint8_t*>(mapped);
      std::copy(src, src + out.pixels.size(), out.pixels.begin());
      vkUnmapMemory(device_, stagingMemory);
    }
  }

  vkDestroyCommandPool(device_, commandPool, Allocator());
  vkFreeMemory(device_, stagingMemory, Allocator());
  vkDestroyBuffer(device_, stagingBuffer, Allocator());
  return out;
}

void VulkanContext::cleanupHeadlessResources()
{
  if (device_ == VK_NULL_HANDLE) {
    return;
  }
  if (headlessFence_ != VK_NULL_HANDLE) {
    vkDestroyFence(device_, headlessFence_, Allocator());
    headlessFence_ = VK_NULL_HANDLE;
  }
  if (headlessCommandPool_ != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device_, headlessCommandPool_, Allocator());
    headlessCommandPool_ = VK_NULL_HANDLE;
    headlessCommandBuffer_ = VK_NULL_HANDLE;
  }
  if (headlessFramebuffer_ != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(device_, headlessFramebuffer_, Allocator());
    headlessFramebuffer_ = VK_NULL_HANDLE;
  }
  if (headlessColorView_ != VK_NULL_HANDLE) {
    vkDestroyImageView(device_, headlessColorView_, Allocator());
    headlessColorView_ = VK_NULL_HANDLE;
  }
  if (headlessDepthView_ != VK_NULL_HANDLE) {
    vkDestroyImageView(device_, headlessDepthView_, Allocator());
    headlessDepthView_ = VK_NULL_HANDLE;
  }
  if (headlessColorImage_ != VK_NULL_HANDLE) {
    vkDestroyImage(device_, headlessColorImage_, Allocator());
    headlessColorImage_ = VK_NULL_HANDLE;
  }
  if (headlessDepthImage_ != VK_NULL_HANDLE) {
    vkDestroyImage(device_, headlessDepthImage_, Allocator());
    headlessDepthImage_ = VK_NULL_HANDLE;
  }
  if (headlessColorMemory_ != VK_NULL_HANDLE) {
    vkFreeMemory(device_, headlessColorMemory_, Allocator());
    headlessColorMemory_ = VK_NULL_HANDLE;
  }
  if (headlessDepthMemory_ != VK_NULL_HANDLE) {
    vkFreeMemory(device_, headlessDepthMemory_, Allocator());
    headlessDepthMemory_ = VK_NULL_HANDLE;
  }
  if (headlessRenderPass_ != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device_, headlessRenderPass_, Allocator());
    headlessRenderPass_ = VK_NULL_HANDLE;
  }
  headlessWidth_ = 0;
  headlessHeight_ = 0;
}

void VulkanContext::cleanupWindow()
{
  if (headless_) {
    return;
  }
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
#ifdef __APPLE__
  if (moltenVkLibrary_) {
    dlclose(moltenVkLibrary_);
    moltenVkLibrary_ = nullptr;
  }
#endif
  physicalDevice_ = VK_NULL_HANDLE;
  queueFamily_ = UINT32_MAX;
  queue_ = VK_NULL_HANDLE;
}

std::unique_ptr<GraphicsContext> CreateVulkanGraphicsContext()
{
  return std::make_unique<VulkanContext>();
}
