#pragma once

#include <memory>

struct NativeWindowHandle;
class MetalContext;
class VulkanContext;

struct ImGuiFrameSize {
  int framebufferWidth = 1;
  int framebufferHeight = 1;
  float displayWidth = 1.0f;
  float displayHeight = 1.0f;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;
};

class ImGuiBackend {
public:
  virtual ~ImGuiBackend() = default;
  virtual bool init() = 0;
  virtual bool newFrame(const ImGuiFrameSize& size) = 0;
  virtual void render() = 0;
  virtual void shutdown() = 0;
};

std::unique_ptr<ImGuiBackend> CreateGlfwOpenGLImGuiBackend(
  NativeWindowHandle window);
std::unique_ptr<ImGuiBackend> CreateHeadlessOpenGLImGuiBackend(int width,
                                                               int height);
std::unique_ptr<ImGuiBackend> CreateHeadlessVulkanImGuiBackend(
  VulkanContext& context,
  int width,
  int height);
std::unique_ptr<ImGuiBackend> CreateHeadlessMetalImGuiBackend(
  MetalContext& context,
  int width,
  int height);
std::unique_ptr<ImGuiBackend> CreateGlfwVulkanImGuiBackend(
  NativeWindowHandle window,
  VulkanContext& context);
std::unique_ptr<ImGuiBackend> CreateGlfwMetalImGuiBackend(
  NativeWindowHandle window,
  MetalContext& context);

void InitImGuiContext(std::unique_ptr<ImGuiBackend> backend);
bool BeginImGuiFrame(const ImGuiFrameSize& size);
bool BeginImGuiFrame(int width, int height);
void EndImGuiFrame();
void ShutdownImGuiContext();
