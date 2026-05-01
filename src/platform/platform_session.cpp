#include "platform/platform_session.h"

#include "app/state/app_state.h"
#ifndef PARTICLE_VIS_HEADLESS_ONLY
#include "app/app_callbacks.h"
#endif
#include "platform/imgui_context.h"
#include "render/render_backend.h"
#ifdef PARTICLE_VIS_ENABLE_METAL_BACKEND
#include "platform/metal_context.h"
#endif
#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
#include "platform/vulkan_context.h"
#endif

#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

#ifndef NONATIVEFILEDIALOG
#include <nfd.h>
#endif

namespace {

bool WantsVulkanPlatform()
{
  const char* backend = std::getenv("PARTICLE_VIS_RENDER_BACKEND");
  return backend &&
         (std::string(backend) == "vulkan" || std::string(backend) == "vk");
}

bool WantsMetalPlatform()
{
  const char* backend = std::getenv("PARTICLE_VIS_RENDER_BACKEND");
  return backend &&
         (std::string(backend) == "metal" || std::string(backend) == "mtl");
}

int EnvInt(const char* name, int fallback)
{
  const char* value = std::getenv(name);
  if (!value || value[0] == '\0') {
    return fallback;
  }
  try {
    return std::max(1, std::stoi(value));
  } catch (...) {
    return fallback;
  }
}

std::unique_ptr<GraphicsContext> CreateSelectedGraphicsContext()
{
#ifdef PARTICLE_VIS_ENABLE_METAL_BACKEND
  if (WantsMetalPlatform()) {
    return CreateMetalGraphicsContext();
  }
#else
  if (WantsMetalPlatform()) {
    std::cerr << "Metal platform is not linked; using OpenGL platform."
              << std::endl;
  }
#endif
#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
  if (WantsVulkanPlatform()) {
    return CreateVulkanGraphicsContext();
  }
#else
  if (WantsVulkanPlatform()) {
    std::cerr << "Vulkan platform is not linked; using OpenGL platform."
              << std::endl;
  }
#endif
  return CreateDefaultGraphicsContext();
}

}

PlatformSession::PlatformSession()
{
  graphics_ = CreateSelectedGraphicsContext();
  if (graphics_) {
    localPresenter_ =
      std::make_unique<LocalFramePresenter>(window_, *graphics_);
  }
}

PlatformSession::~PlatformSession()
{
  shutdown();
}

bool PlatformSession::init(AppState& app, CallbackContext& callbackCtx)
{
  shutdownCalled_ = false;
  if (!graphics_ || !localPresenter_) {
    return false;
  }

  const int initialWidth = EnvInt("PARTICLE_VIS_WINDOW_WIDTH", 1280);
  const int initialHeight = EnvInt("PARTICLE_VIS_WINDOW_HEIGHT", 720);

  bool initialized = false;
#ifdef PYTHON_BRIDGE
  const char* remoteFrameEndpoint =
    std::getenv("PARTICLE_VIS_REMOTE_FRAME_ENDPOINT");
  const bool remoteMode =
    remoteFrameEndpoint && remoteFrameEndpoint[0] != '\0';
  const char* headlessEnv = std::getenv("PARTICLE_VIS_HEADLESS");
  const bool allowHeadless = !headlessEnv || std::string(headlessEnv) != "0";
  if (remoteMode && allowHeadless) {
    initialized = window_.initHeadless(initialWidth, initialHeight);
    if (initialized) {
      initialized = graphics_->initHeadless(window_.framebufferWidth(),
                                            window_.framebufferHeight());
    }
    if (!initialized) {
      shutdown();
      return false;
    }
  }
#endif

  if (!initialized) {
    initialized =
      window_.init(initialWidth,
                   initialHeight,
                   "3D Particle Visualization",
                   [this]() {
                     graphics_->configureWindowHints();
                   });
    if (initialized) {
      if (window_.hasWindow()) {
        initialized = graphics_->initFromWindow(window_.nativeWindowHandle());
      } else {
        initialized = graphics_->initHeadless(window_.framebufferWidth(),
                                              window_.framebufferHeight());
      }
    }
  }

  if (!initialized) {
    shutdown();
    return false;
  }

  callbackCtx.app = &app;
  callbackCtx.window = &window_;

  std::unique_ptr<ImGuiBackend> imguiBackend;
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (window_.hasWindow()) {
    AttachAppCallbacks(window_, callbackCtx);
#ifdef PARTICLE_VIS_ENABLE_METAL_BACKEND
    if (auto* metal = dynamic_cast<MetalContext*>(graphics_.get())) {
      imguiBackend =
        CreateGlfwMetalImGuiBackend(window_.nativeWindowHandle(), *metal);
    } else
#endif
#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
    if (auto* vulkan = dynamic_cast<VulkanContext*>(graphics_.get())) {
      imguiBackend =
        CreateGlfwVulkanImGuiBackend(window_.nativeWindowHandle(), *vulkan);
    } else
#endif
    {
    imguiBackend =
      CreateGlfwOpenGLImGuiBackend(window_.nativeWindowHandle());
    }
  } else {
#endif
#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
    if (auto* vulkan = dynamic_cast<VulkanContext*>(graphics_.get())) {
      imguiBackend =
        CreateHeadlessVulkanImGuiBackend(*vulkan,
                                         window_.framebufferWidth(),
                                         window_.framebufferHeight());
    } else
#endif
    {
    imguiBackend =
      CreateHeadlessOpenGLImGuiBackend(window_.framebufferWidth(),
                                       window_.framebufferHeight());
    }
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  }
#endif
  if (!imguiBackend) {
    shutdown();
    return false;
  }
  InitImGuiContext(std::move(imguiBackend));
  imguiInitialized_ = true;

#ifndef NONATIVEFILEDIALOG
  if (NFD_Init() != NFD_OKAY) {
    std::cerr << "NFD_Init failed: " << NFD_GetError() << std::endl;
  } else {
    nfdInitialized_ = true;
  }
#endif

  if (const char* endpoint = std::getenv("PARTICLE_VIS_REMOTE_FRAME_ENDPOINT")) {
    if (endpoint[0] != '\0') {
      remotePresenter_ =
        std::make_unique<RemoteFramePresenter>(window_,
                                               *graphics_,
                                               std::string(endpoint));
    }
  }

  return true;
}

void PlatformSession::startRemoteInput(AppState& app)
{
  if (const char* endpoint = std::getenv("PARTICLE_VIS_REMOTE_INPUT_ENDPOINT")) {
    if (endpoint[0] != '\0') {
      remoteInput_.start(endpoint, app.runtime.inputEvents);
    }
  }
}

IFramePresenter& PlatformSession::presenter()
{
  if (remotePresenter_ && remotePresenter_->active()) {
    return *remotePresenter_;
  }
  return *localPresenter_;
}

std::unique_ptr<RenderBackend> PlatformSession::createRenderBackend()
{
#ifdef PARTICLE_VIS_ENABLE_METAL_BACKEND
  if (auto* metal = dynamic_cast<MetalContext*>(graphics_.get())) {
    return CreateMetalRenderBackend(*metal);
  }
#endif
#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
  if (auto* vulkan = dynamic_cast<VulkanContext*>(graphics_.get())) {
    return CreateVulkanRenderBackend(*vulkan);
  }
#endif
  return nullptr;
}

void PlatformSession::shutdown()
{
  if (shutdownCalled_) {
    return;
  }
  shutdownCalled_ = true;

  remoteInput_.stop();

#ifndef NONATIVEFILEDIALOG
  if (nfdInitialized_) {
    NFD_Quit();
    nfdInitialized_ = false;
  }
#endif

  if (imguiInitialized_) {
    ShutdownImGuiContext();
    imguiInitialized_ = false;
  }
  if (graphics_) {
    graphics_->destroy();
  }
  window_.destroy();
}
