#include "platform/imgui_context.h"

#include "platform/window_backend.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include "implot.h"

#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
#include "platform/vulkan_context.h"
#include <imgui_impl_vulkan.h>
#endif
#ifdef PARTICLE_VIS_ENABLE_METAL_BACKEND
#include "platform/metal_context.h"
#endif

#ifndef PARTICLE_VIS_HEADLESS_ONLY
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#endif

namespace {

class HeadlessOpenGLImGuiBackend final : public ImGuiBackend {
public:
  HeadlessOpenGLImGuiBackend(int width, int height)
    : width_(width)
    , height_(height)
  {
  }

  bool init() override
  {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    setDisplaySize(width_, height_);

    ImGui_ImplOpenGL3_Init("#version 330");
    initialized_ = true;
    return true;
  }

  void newFrame(int width, int height) override
  {
    ImGui_ImplOpenGL3_NewFrame();
    setDisplaySize(width, height);
    ImGui::NewFrame();
  }

  void render() override
  {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }

  void shutdown() override
  {
    if (!initialized_) {
      return;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();
    initialized_ = false;
  }

private:
  void setDisplaySize(int width, int height)
  {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width),
                            static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
  }

  int width_ = 1280;
  int height_ = 720;
  bool initialized_ = false;
};

#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
class HeadlessVulkanImGuiBackend final : public ImGuiBackend {
public:
  HeadlessVulkanImGuiBackend(VulkanContext& context, int width, int height)
    : context_(&context)
    , width_(width)
    , height_(height)
  {
  }

  bool init() override
  {
    if (!context_) {
      return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    setDisplaySize(width_, height_);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = context_->instance();
    initInfo.PhysicalDevice = context_->physicalDevice();
    initInfo.Device = context_->device();
    initInfo.QueueFamily = context_->queueFamily();
    initInfo.Queue = context_->queue();
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = context_->descriptorPool();
    initInfo.RenderPass = context_->renderPass();
    initInfo.Subpass = 0;
    initInfo.MinImageCount = context_->minImageCount();
    initInfo.ImageCount = context_->imageCount();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator = nullptr;
    ImGui_ImplVulkan_Init(&initInfo);

    initialized_ = true;
    return true;
  }

  void newFrame(int width, int height) override
  {
    ImGui_ImplVulkan_NewFrame();
    setDisplaySize(width, height);
    ImGui::NewFrame();
  }

  void render() override
  {
    ImGui::Render();
    context_->renderImGuiDrawData(ImGui::GetDrawData());
  }

  void shutdown() override
  {
    if (!initialized_) {
      return;
    }
    if (context_ && context_->device() != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(context_->device());
    }
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();
    initialized_ = false;
  }

private:
  void setDisplaySize(int width, int height)
  {
    width_ = width;
    height_ = height;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width),
                            static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
  }

  VulkanContext* context_ = nullptr;
  int width_ = 1280;
  int height_ = 720;
  bool initialized_ = false;
};
#endif

#ifndef PARTICLE_VIS_HEADLESS_ONLY
class GlfwOpenGLImGuiBackend final : public ImGuiBackend {
public:
  explicit GlfwOpenGLImGuiBackend(GLFWwindow* window)
    : window_(window)
  {
  }

  bool init() override
  {
    if (!window_) {
      return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    initialized_ = true;
    return true;
  }

  void newFrame(int /*width*/, int /*height*/) override
  {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
  }

  void render() override
  {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }

  void shutdown() override
  {
    if (!initialized_) {
      return;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();
    initialized_ = false;
  }

private:
  GLFWwindow* window_ = nullptr;
  bool initialized_ = false;
};

#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
class GlfwVulkanImGuiBackend final : public ImGuiBackend {
public:
  GlfwVulkanImGuiBackend(GLFWwindow* window, VulkanContext& context)
    : window_(window)
    , context_(&context)
  {
  }

  bool init() override
  {
    if (!window_ || !context_) {
      return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImGui_ImplGlfw_InitForVulkan(window_, true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = context_->instance();
    initInfo.PhysicalDevice = context_->physicalDevice();
    initInfo.Device = context_->device();
    initInfo.QueueFamily = context_->queueFamily();
    initInfo.Queue = context_->queue();
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = context_->descriptorPool();
    initInfo.RenderPass = context_->renderPass();
    initInfo.Subpass = 0;
    initInfo.MinImageCount = context_->minImageCount();
    initInfo.ImageCount = context_->imageCount();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator = nullptr;
    ImGui_ImplVulkan_Init(&initInfo);

    initialized_ = true;
    return true;
  }

  void newFrame(int /*width*/, int /*height*/) override
  {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
  }

  void render() override
  {
    ImGui::Render();
    context_->renderImGuiDrawData(ImGui::GetDrawData());
  }

  void shutdown() override
  {
    if (!initialized_) {
      return;
    }
    if (context_ && context_->device() != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(context_->device());
    }
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();
    initialized_ = false;
  }

private:
  GLFWwindow* window_ = nullptr;
  VulkanContext* context_ = nullptr;
  bool initialized_ = false;
};
#endif

#ifdef PARTICLE_VIS_ENABLE_METAL_BACKEND
class GlfwMetalImGuiBackend final : public ImGuiBackend {
public:
  GlfwMetalImGuiBackend(GLFWwindow* window, MetalContext& context)
    : window_(window)
    , context_(&context)
  {
  }

  bool init() override
  {
    if (!window_ || !context_) {
      return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    if (!context_->initImGuiRenderer()) {
      ImGui_ImplGlfw_Shutdown();
      ImGui::DestroyContext();
      ImPlot::DestroyContext();
      return false;
    }

    initialized_ = true;
    return true;
  }

  void newFrame(int width, int height) override
  {
    if (!context_->beginFrame(width, height)) {
      frameReady_ = false;
      return;
    }
    frameReady_ = true;
    context_->newImGuiFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
  }

  void render() override
  {
    if (!frameReady_) {
      return;
    }
    ImGui::Render();
    context_->renderImGuiDrawData(ImGui::GetDrawData());
    frameReady_ = false;
  }

  void shutdown() override
  {
    if (!initialized_) {
      return;
    }
    if (context_) {
      context_->shutdownImGuiRenderer();
    }
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();
    initialized_ = false;
    frameReady_ = false;
  }

private:
  GLFWwindow* window_ = nullptr;
  MetalContext* context_ = nullptr;
  bool initialized_ = false;
  bool frameReady_ = false;
};
#endif
#endif

std::unique_ptr<ImGuiBackend> g_backend;

} // namespace

std::unique_ptr<ImGuiBackend> CreateGlfwOpenGLImGuiBackend(
  NativeWindowHandle window)
{
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (window.backend != NativeWindowBackend::GLFW || !window.handle) {
    return nullptr;
  }
  return std::make_unique<GlfwOpenGLImGuiBackend>(
    static_cast<GLFWwindow*>(window.handle));
#else
  (void)window;
  return nullptr;
#endif
}

std::unique_ptr<ImGuiBackend> CreateHeadlessOpenGLImGuiBackend(int width,
                                                               int height)
{
  return std::make_unique<HeadlessOpenGLImGuiBackend>(width, height);
}

std::unique_ptr<ImGuiBackend> CreateHeadlessVulkanImGuiBackend(
  VulkanContext& context,
  int width,
  int height)
{
#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
  return std::make_unique<HeadlessVulkanImGuiBackend>(context, width, height);
#else
  (void)context;
  (void)width;
  (void)height;
  return nullptr;
#endif
}

std::unique_ptr<ImGuiBackend> CreateGlfwVulkanImGuiBackend(
  NativeWindowHandle window,
  VulkanContext& context)
{
#if !defined(PARTICLE_VIS_HEADLESS_ONLY) && defined(PARTICLE_VIS_ENABLE_VULKAN_BACKEND)
  if (window.backend != NativeWindowBackend::GLFW || !window.handle) {
    return nullptr;
  }
  return std::make_unique<GlfwVulkanImGuiBackend>(
    static_cast<GLFWwindow*>(window.handle),
    context);
#else
  (void)window;
  (void)context;
  return nullptr;
#endif
}

std::unique_ptr<ImGuiBackend> CreateGlfwMetalImGuiBackend(
  NativeWindowHandle window,
  MetalContext& context)
{
#if !defined(PARTICLE_VIS_HEADLESS_ONLY) && defined(PARTICLE_VIS_ENABLE_METAL_BACKEND)
  if (window.backend != NativeWindowBackend::GLFW || !window.handle) {
    return nullptr;
  }
  return std::make_unique<GlfwMetalImGuiBackend>(
    static_cast<GLFWwindow*>(window.handle),
    context);
#else
  (void)window;
  (void)context;
  return nullptr;
#endif
}

void InitImGuiContext(std::unique_ptr<ImGuiBackend> backend)
{
  ShutdownImGuiContext();

  if (!backend || !backend->init()) {
    return;
  }
  g_backend = std::move(backend);
}

void BeginImGuiFrame(int width, int height)
{
  if (g_backend) {
    g_backend->newFrame(width, height);
  }
}

void EndImGuiFrame()
{
  if (g_backend) {
    g_backend->render();
  }
}

void ShutdownImGuiContext()
{
  if (g_backend) {
    g_backend->shutdown();
    g_backend.reset();
  }
}
