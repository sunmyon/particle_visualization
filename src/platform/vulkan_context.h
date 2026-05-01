#pragma once

#include "platform/graphics_context.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

struct GLFWwindow;
struct ImDrawData;
struct ImGui_ImplVulkanH_Window;

class VulkanContext final : public GraphicsContext {
public:
  using PreRenderCallback = std::function<void(VkCommandBuffer)>;
  using PreImGuiDrawCallback = std::function<void(VkCommandBuffer)>;

  VulkanContext();
  ~VulkanContext() override;

  void configureWindowHints() const override;
  bool initFromWindow(NativeWindowHandle window) override;
  bool initHeadless(int width, int height) override;
  void destroy() override;
  void present(NativeWindowHandle window) override;
  RenderedFrame readDefaultFramebuffer(int width, int height) override;
  bool isHeadless() const override { return headless_; }

  void renderImGuiDrawData(ImDrawData* drawData);
  void setPreRenderCallback(PreRenderCallback callback);
  void setPreImGuiDrawCallback(PreImGuiDrawCallback callback);

  VkInstance instance() const { return instance_; }
  VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
  VkDevice device() const { return device_; }
  std::uint32_t queueFamily() const { return queueFamily_; }
  VkQueue queue() const { return queue_; }
  VkDescriptorPool descriptorPool() const { return descriptorPool_; }
  VkRenderPass renderPass() const;
  std::uint32_t minImageCount() const { return minImageCount_; }
  std::uint32_t imageCount() const;
  VkExtent2D swapchainExtent() const;

private:
  bool createInstance();
  bool createDevice();
  bool createDescriptorPool();
  bool createSurfaceAndWindow(GLFWwindow* window);
  bool createHeadlessTarget(int width, int height);
  bool createHeadlessRenderPass();
  bool createHeadlessImage(VkFormat format,
                           VkImageUsageFlags usage,
                           VkImageAspectFlags aspect,
                           VkImage& image,
                           VkDeviceMemory& memory,
                           VkImageView& view);
  RenderedFrame readHeadlessFramebuffer(int width, int height);
  void resizeIfNeeded();
  bool rebuildRenderPassWithDepth();
  void cleanupDepthResources();
  void cleanupHeadlessResources();
  void cleanupWindow();
  void cleanupVulkan();

  GLFWwindow* window_ = nullptr;
  bool headless_ = false;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  std::uint32_t queueFamily_ = UINT32_MAX;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
  ImGui_ImplVulkanH_Window* windowData_ = nullptr;
  std::uint32_t minImageCount_ = 2;
  bool swapChainRebuild_ = false;
  bool frameRecorded_ = false;
  PreRenderCallback preRender_;
  PreImGuiDrawCallback preImGuiDraw_;

  VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
  std::vector<VkImage> depthImages_;
  std::vector<VkDeviceMemory> depthMemories_;
  std::vector<VkImageView> depthImageViews_;

  int headlessWidth_ = 0;
  int headlessHeight_ = 0;
  VkFormat headlessColorFormat_ = VK_FORMAT_R8G8B8A8_UNORM;
  VkRenderPass headlessRenderPass_ = VK_NULL_HANDLE;
  VkFramebuffer headlessFramebuffer_ = VK_NULL_HANDLE;
  VkImage headlessColorImage_ = VK_NULL_HANDLE;
  VkDeviceMemory headlessColorMemory_ = VK_NULL_HANDLE;
  VkImageView headlessColorView_ = VK_NULL_HANDLE;
  VkImage headlessDepthImage_ = VK_NULL_HANDLE;
  VkDeviceMemory headlessDepthMemory_ = VK_NULL_HANDLE;
  VkImageView headlessDepthView_ = VK_NULL_HANDLE;
  VkCommandPool headlessCommandPool_ = VK_NULL_HANDLE;
  VkCommandBuffer headlessCommandBuffer_ = VK_NULL_HANDLE;
  VkFence headlessFence_ = VK_NULL_HANDLE;
  void* moltenVkLibrary_ = nullptr;
};

std::unique_ptr<GraphicsContext> CreateVulkanGraphicsContext();
