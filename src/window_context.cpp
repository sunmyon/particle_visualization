#include <glad/glad.h>
#include "window_context.h"

#include <imgui.h>
#include <iostream>
#include <cstdlib>
#include <chrono>
#include <string>

#ifdef PARTICLE_VIS_HAVE_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;

void GlfwErrorCallback(int error, const char* description)
{
  std::cerr << "GLFW error " << error << ": "
            << (description ? description : "(no description)") << std::endl;
}

const char* EnvOrUnset(const char* name)
{
  const char* value = std::getenv(name);
  return (value && value[0] != '\0') ? value : "(unset)";
}

Clock::time_point& StartTime()
{
  static Clock::time_point t0 = Clock::now();
  return t0;
}

#ifdef PARTICLE_VIS_HAVE_EGL
bool HasExtension(const char* extensions, const char* name)
{
  if (!extensions || !name || name[0] == '\0') return false;

  const std::string haystack(extensions);
  const std::string needle(name);
  size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    const bool startOk = (pos == 0) || (haystack[pos - 1] == ' ');
    const size_t end = pos + needle.size();
    const bool endOk = (end == haystack.size()) || (haystack[end] == ' ');
    if (startOk && endOk) return true;
    pos = end;
  }
  return false;
}

void PrintEglError(const char* label)
{
  std::cerr << label << " EGL error=0x"
            << std::hex << eglGetError() << std::dec << std::endl;
}

EGLDisplay GetPreferredEglDisplay()
{
  const char* clientExtensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
  std::cerr << "EGL client extensions: "
            << (clientExtensions ? clientExtensions : "(null)") << std::endl;

  const bool canPlatformBase =
    HasExtension(clientExtensions, "EGL_EXT_platform_base") ||
    HasExtension(clientExtensions, "EGL_KHR_platform_base");
  const bool canPlatformDevice =
    HasExtension(clientExtensions, "EGL_EXT_platform_device");
  const bool canDeviceEnumeration =
    HasExtension(clientExtensions, "EGL_EXT_device_enumeration") ||
    HasExtension(clientExtensions, "EGL_EXT_device_base");
  const bool canSurfaceless =
    HasExtension(clientExtensions, "EGL_MESA_platform_surfaceless");

  const char* requested = std::getenv("PARTICLE_VIS_EGL_PLATFORM");
  const bool forceDefault = requested && std::string(requested) == "default";
  const bool forceSurfaceless =
    requested && std::string(requested) == "surfaceless";
  const bool skipDevice = forceDefault || forceSurfaceless;
  const bool preferSurfaceless = !forceDefault;

  if (!skipDevice && canPlatformBase && canPlatformDevice && canDeviceEnumeration) {
    auto queryDevices =
      reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(
        eglGetProcAddress("eglQueryDevicesEXT"));
    auto getPlatformDisplay =
      reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));

    if (queryDevices && getPlatformDisplay) {
      EGLDeviceEXT devices[16] = {};
      EGLint count = 0;
      if (queryDevices(16, devices, &count) && count > 0) {
        int deviceIndex = 0;
        if (const char* indexEnv = std::getenv("PARTICLE_VIS_EGL_DEVICE_INDEX")) {
          deviceIndex = std::atoi(indexEnv);
        }
        if (deviceIndex < 0 || deviceIndex >= count) {
          deviceIndex = 0;
        }

        std::cerr << "EGL: found " << count << " device(s); using device "
                  << deviceIndex << std::endl;
        std::cerr << "EGL: eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT)"
                  << std::endl;
        EGLDisplay display =
          getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT,
                             devices[deviceIndex],
                             nullptr);
        if (display != EGL_NO_DISPLAY) {
          return display;
        }
        PrintEglError("EGL device display unavailable.");
      } else {
        PrintEglError("EGL device enumeration failed.");
      }
    }
  }

  if (preferSurfaceless && canPlatformBase && canSurfaceless) {
    auto getPlatformDisplay =
      reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (getPlatformDisplay) {
      std::cerr << "EGL: eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA)"
                << std::endl;
      EGLDisplay display =
        getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                           EGL_DEFAULT_DISPLAY,
                           nullptr);
      if (display != EGL_NO_DISPLAY) {
        return display;
      }
      PrintEglError("Surfaceless EGL display unavailable.");
    }
  }

  std::cerr << "EGL: eglGetDisplay(EGL_DEFAULT_DISPLAY)" << std::endl;
  return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}
#endif
}

bool WindowContext::init(int width, int height, const char* title)
{
#ifdef PARTICLE_VIS_HEADLESS_ONLY
  (void)title;
  return initHeadless(width, height);
#else
  initialWidth_ = width;
  initialHeight_ = height;
  headless_ = false;
  closeRequested_ = false;

  glfwSetErrorCallback(GlfwErrorCallback);

#if defined(__linux__) && defined(GLFW_PLATFORM) && defined(GLFW_PLATFORM_X11)
  // In SSH X forwarding sessions DISPLAY is set, but some systems still let
  // GLFW probe Wayland first and fail on missing XDG_RUNTIME_DIR.
  if (std::getenv("DISPLAY") && !std::getenv("PARTICLE_VIS_GLFW_PLATFORM")) {
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  }
#endif

  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    std::cerr << "DISPLAY=" << EnvOrUnset("DISPLAY")
              << " WAYLAND_DISPLAY=" << EnvOrUnset("WAYLAND_DISPLAY")
              << " XDG_RUNTIME_DIR=" << EnvOrUnset("XDG_RUNTIME_DIR")
              << std::endl;
    return false;
  }
  glfwInitialized_ = true;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  handle_ = glfwCreateWindow(initialWidth_, initialHeight_, title, nullptr, nullptr);
  if (!handle_) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    std::cerr << "DISPLAY=" << EnvOrUnset("DISPLAY")
              << " WAYLAND_DISPLAY=" << EnvOrUnset("WAYLAND_DISPLAY")
              << " XDG_RUNTIME_DIR=" << EnvOrUnset("XDG_RUNTIME_DIR")
              << std::endl;
    glfwTerminate();
    glfwInitialized_ = false;
    return false;
  }

  glfwMakeContextCurrent(handle_);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cerr << "Failed to initialize GLAD" << std::endl;
    glfwDestroyWindow(handle_);
    handle_ = nullptr;
    glfwTerminate();
    glfwInitialized_ = false;
    return false;
  }

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_PROGRAM_POINT_SIZE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  int fbW = 0, fbH = 0;
  glfwGetFramebufferSize(handle_, &fbW, &fbH);
  updateFramebufferSize(fbW, fbH);

  return true;
#endif
}

bool WindowContext::initHeadless(int width, int height)
{
  initialWidth_ = width;
  initialHeight_ = height;
  headless_ = true;
  closeRequested_ = false;

#ifndef PARTICLE_VIS_HAVE_EGL
  std::cerr << "EGL headless context was not enabled at build time." << std::endl;
  return false;
#else
  std::cerr << "Trying EGL headless OpenGL context..." << std::endl;

  EGLDisplay display = GetPreferredEglDisplay();
  if (display == EGL_NO_DISPLAY) {
    PrintEglError("Failed to get EGL display.");
    return false;
  }

  EGLint major = 0;
  EGLint minor = 0;
  std::cerr << "EGL: eglInitialize" << std::endl;
  if (!eglInitialize(display, &major, &minor)) {
    PrintEglError("Failed to initialize EGL.");
    return false;
  }

  std::cerr << "EGL initialized: " << major << "." << minor << std::endl;
  std::cerr << "EGL vendor: "
            << (eglQueryString(display, EGL_VENDOR)
                ? eglQueryString(display, EGL_VENDOR) : "(null)")
            << std::endl;
  std::cerr << "EGL extensions: "
            << (eglQueryString(display, EGL_EXTENSIONS)
                ? eglQueryString(display, EGL_EXTENSIONS) : "(null)")
            << std::endl;

  std::cerr << "EGL: eglBindAPI(EGL_OPENGL_API)" << std::endl;
  if (!eglBindAPI(EGL_OPENGL_API)) {
    PrintEglError("Failed to bind EGL OpenGL API.");
    eglTerminate(display);
    return false;
  }

  const EGLint configAttribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 24,
    EGL_STENCIL_SIZE, 8,
    EGL_NONE
  };

  EGLConfig config = nullptr;
  EGLint numConfigs = 0;
  std::cerr << "EGL: eglChooseConfig" << std::endl;
  if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs) ||
      numConfigs <= 0) {
    PrintEglError("Failed to choose EGL framebuffer config.");
    eglTerminate(display);
    return false;
  }

  const EGLint surfaceAttribs[] = {
    EGL_WIDTH, width,
    EGL_HEIGHT, height,
    EGL_NONE
  };
  std::cerr << "EGL: eglCreatePbufferSurface" << std::endl;
  EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttribs);
  if (surface == EGL_NO_SURFACE) {
    PrintEglError("Failed to create EGL pbuffer surface.");
    eglTerminate(display);
    return false;
  }

  const EGLint strictContextAttribs[] = {
#ifdef EGL_CONTEXT_MAJOR_VERSION
    EGL_CONTEXT_MAJOR_VERSION, 3,
    EGL_CONTEXT_MINOR_VERSION, 3,
#endif
    EGL_NONE
  };
  const EGLint defaultContextAttribs[] = {
    EGL_NONE
  };
  const char* strictContextEnv = std::getenv("PARTICLE_VIS_EGL_STRICT_CONTEXT");
  const bool strictContext =
    strictContextEnv && std::string(strictContextEnv) == "1";
  const EGLint* contextAttribs =
    strictContext ? strictContextAttribs : defaultContextAttribs;

  std::cerr << "EGL: eglCreateContext"
            << (strictContext ? " (OpenGL 3.3 request)" : " (default context)")
            << std::endl;
  EGLContext context =
    eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
  if (context == EGL_NO_CONTEXT) {
    PrintEglError("Failed to create EGL OpenGL context.");
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return false;
  }

  std::cerr << "EGL: eglMakeCurrent" << std::endl;
  if (!eglMakeCurrent(display, surface, surface, context)) {
    PrintEglError("Failed to make EGL context current.");
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return false;
  }

  std::cerr << "EGL: gladLoadGLLoader" << std::endl;
  if (!gladLoadGLLoader((GLADloadproc)eglGetProcAddress)) {
    std::cerr << "Failed to initialize GLAD from EGL" << std::endl;
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return false;
  }

  eglDisplay_ = display;
  eglSurface_ = surface;
  eglContext_ = context;

  std::cerr << "OpenGL vendor: "
            << (glGetString(GL_VENDOR)
                ? reinterpret_cast<const char*>(glGetString(GL_VENDOR)) : "(null)")
            << std::endl;
  std::cerr << "OpenGL renderer: "
            << (glGetString(GL_RENDERER)
                ? reinterpret_cast<const char*>(glGetString(GL_RENDERER)) : "(null)")
            << std::endl;
  std::cerr << "OpenGL version: "
            << (glGetString(GL_VERSION)
                ? reinterpret_cast<const char*>(glGetString(GL_VERSION)) : "(null)")
            << std::endl;

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_PROGRAM_POINT_SIZE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  updateFramebufferSize(width, height);

  std::cerr << "Initialized EGL headless OpenGL context "
            << major << "." << minor << " (" << width << "x" << height
            << ")" << std::endl;
  return true;
#endif
}

void WindowContext::destroy()
{
#ifdef PARTICLE_VIS_HAVE_EGL
  if (eglDisplay_) {
    EGLDisplay display = static_cast<EGLDisplay>(eglDisplay_);
    EGLSurface surface = static_cast<EGLSurface>(eglSurface_);
    EGLContext context = static_cast<EGLContext>(eglContext_);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
    if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
    eglTerminate(display);
    eglDisplay_ = nullptr;
    eglSurface_ = nullptr;
    eglContext_ = nullptr;
  }
#endif

#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (handle_) {
    glfwDestroyWindow(handle_);
    handle_ = nullptr;
  }
  if (glfwInitialized_) {
    glfwTerminate();
    glfwInitialized_ = false;
  }
#endif
}

#ifndef PARTICLE_VIS_HEADLESS_ONLY
void WindowContext::attachCallbacks(GLFWcursorposfun mouseCb,
                                    GLFWscrollfun scrollCb,
                                    GLFWkeyfun keyCb,
                                    GLFWframebuffersizefun framebufferCb)
{
  if (!handle_) return;

  glfwSetCursorPosCallback(handle_, mouseCb);
  glfwSetScrollCallback(handle_, scrollCb);
  glfwSetKeyCallback(handle_, keyCb);
  glfwSetFramebufferSizeCallback(handle_, framebufferCb);
}
#endif

void WindowContext::requestClose()
{
  closeRequested_ = true;
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (handle_) {
    glfwSetWindowShouldClose(handle_, true);
  }
#endif
}

void WindowContext::pollEvents()
{
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (handle_) {
    glfwPollEvents();
  }
#endif
}

bool WindowContext::shouldClose() const
{
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (handle_) {
    return glfwWindowShouldClose(handle_);
  }
#endif
  return closeRequested_;
}

double WindowContext::timeSeconds() const
{
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (glfwInitialized_) {
    return glfwGetTime();
  }
#endif

  const auto now = Clock::now();
  return std::chrono::duration<double>(now - StartTime()).count();
}

void WindowContext::present()
{
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (handle_) {
    glfwSwapBuffers(handle_);
  }
#endif
#ifdef PARTICLE_VIS_HAVE_EGL
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  const bool hasWindow = handle_ != nullptr;
#else
  const bool hasWindow = false;
#endif
  if (!hasWindow && eglDisplay_ && eglSurface_) {
    eglSwapBuffers(static_cast<EGLDisplay>(eglDisplay_),
                   static_cast<EGLSurface>(eglSurface_));
  }
#endif
}

void WindowContext::updateFramebufferSize(int width, int height)
{
  framebufferWidth_ = width;
  framebufferHeight_ = height;

#ifdef USE_LETTERBOX
  const float targetAspect =
    static_cast<float>(initialWidth_) / static_cast<float>(initialHeight_);
  const float windowAspect =
    static_cast<float>(width) / static_cast<float>(height);

  if (windowAspect > targetAspect) {
    viewportHeight_ = height;
    viewportWidth_  = static_cast<int>(height * targetAspect);
    viewportX_      = (width - viewportWidth_) / 2;
    viewportY_      = 0;
  } else {
    viewportWidth_  = width;
    viewportHeight_ = static_cast<int>(width / targetAspect);
    viewportX_      = 0;
    viewportY_      = (height - viewportHeight_) / 2;
  }

  glViewport(viewportX_, viewportY_, viewportWidth_, viewportHeight_);
#else
  viewportX_ = 0;
  viewportY_ = 0;
  viewportWidth_ = width;
  viewportHeight_ = height;
  glViewport(0, 0, width, height);
#endif
}

glm::vec2 WindowContext::framebufferToImGui(float px, float py) const
{
  ImGuiIO& io = ImGui::GetIO();

  const float scaleX = io.DisplayFramebufferScale.x;
  const float scaleY = io.DisplayFramebufferScale.y;
  const float fbH    = io.DisplaySize.y * scaleY;

  return glm::vec2(px / scaleX,
                   (fbH - py) / scaleY);
}

glm::vec2 WindowContext::ndcToFramebuffer(const glm::vec3& ndc) const
{
  const float px = viewportX_ +
                   (ndc.x * 0.5f + 0.5f) * static_cast<float>(viewportWidth_);
  const float py = viewportY_ +
                   (ndc.y * 0.5f + 0.5f) * static_cast<float>(viewportHeight_);

  return glm::vec2(px, py);
}

glm::vec2 WindowContext::ndcToImGui(const glm::vec3& ndc) const
{
  glm::vec2 fb = ndcToFramebuffer(ndc);
  return framebufferToImGui(fb.x, fb.y);
}

float WindowContext::projectionAspect() const
{
  if (viewportHeight_ <= 0)
    return 1.0f;

  return static_cast<float>(viewportWidth_) /
         static_cast<float>(viewportHeight_);
}
