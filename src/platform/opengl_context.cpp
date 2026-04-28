#include "platform/opengl_context.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <glad/glad.h>

#ifndef PARTICLE_VIS_HEADLESS_ONLY
#include <GLFW/glfw3.h>
#endif
#include "platform/window_backend.h"

#ifdef PARTICLE_VIS_HAVE_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

namespace {

void EnableDefaultOpenGLState()
{
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_PROGRAM_POINT_SIZE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void PrintOpenGLInfo()
{
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

EGLDisplay GetPreferredEglDisplay(const char* platformOverride = nullptr)
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

  const char* requested =
    (platformOverride && platformOverride[0] != '\0')
      ? platformOverride
      : std::getenv("PARTICLE_VIS_EGL_PLATFORM");
  const bool forceDefault = requested && std::string(requested) == "default";
  const bool forceSurfaceless =
    requested && std::string(requested) == "surfaceless";
  const bool skipDevice = forceDefault || forceSurfaceless;
  const bool preferSurfaceless = !forceDefault;

  if (!skipDevice && canPlatformBase && canPlatformDevice &&
      canDeviceEnumeration) {
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

bool InitializeEglDisplay(EGLDisplay display, EGLint& major, EGLint& minor)
{
  std::cerr << "EGL: eglInitialize" << std::endl;
  if (eglInitialize(display, &major, &minor)) {
    return true;
  }
  PrintEglError("Failed to initialize EGL.");
  return false;
}
#endif

} // namespace

void OpenGLContext::configureWindowHints() const
{
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
#endif
}

bool OpenGLContext::initFromWindow(NativeWindowHandle window)
{
#ifdef PARTICLE_VIS_HEADLESS_ONLY
  (void)window;
  return false;
#else
  if (window.backend != NativeWindowBackend::GLFW || !window.handle) {
    return false;
  }
  auto* handle = static_cast<GLFWwindow*>(window.handle);

  headless_ = false;
  glfwMakeContextCurrent(handle);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cerr << "Failed to initialize GLAD" << std::endl;
    return false;
  }

  PrintOpenGLInfo();
  EnableDefaultOpenGLState();
  return true;
#endif
}

bool OpenGLContext::initHeadless(int width, int height)
{
#ifndef PARTICLE_VIS_HAVE_EGL
  (void)width;
  (void)height;
  std::cerr << "EGL headless context was not enabled at build time." << std::endl;
  return false;
#else
  headless_ = true;
  std::cerr << "Trying EGL headless OpenGL context..." << std::endl;

  EGLDisplay display = GetPreferredEglDisplay();
  if (display == EGL_NO_DISPLAY) {
    PrintEglError("Failed to get EGL display.");
    return false;
  }

  EGLint major = 0;
  EGLint minor = 0;
  if (!InitializeEglDisplay(display, major, minor)) {
    const char* requestedPlatform = std::getenv("PARTICLE_VIS_EGL_PLATFORM");
    const bool platformForced =
      requestedPlatform && requestedPlatform[0] != '\0';

    if (platformForced) {
      return false;
    }

    const char* fallbacks[] = { "surfaceless", "default" };
    bool initialized = false;
    for (const char* fallback : fallbacks) {
      std::cerr << "EGL: retrying with PARTICLE_VIS_EGL_PLATFORM="
                << fallback << std::endl;
      display = GetPreferredEglDisplay(fallback);
      if (display == EGL_NO_DISPLAY) {
        PrintEglError("Failed to get fallback EGL display.");
        continue;
      }
      if (InitializeEglDisplay(display, major, minor)) {
        initialized = true;
        break;
      }
    }

    if (!initialized) {
      return false;
    }
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

  PrintOpenGLInfo();
  EnableDefaultOpenGLState();

  std::cerr << "Initialized EGL headless OpenGL context "
            << major << "." << minor << " (" << width << "x" << height
            << ")" << std::endl;
  return true;
#endif
}

void OpenGLContext::destroy()
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
  headless_ = false;
}

void OpenGLContext::present(NativeWindowHandle window)
{
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (window.backend == NativeWindowBackend::GLFW && window.handle) {
    glfwSwapBuffers(static_cast<GLFWwindow*>(window.handle));
    return;
  }
#else
  (void)window;
#endif

#ifdef PARTICLE_VIS_HAVE_EGL
  if (eglDisplay_ && eglSurface_) {
    eglSwapBuffers(static_cast<EGLDisplay>(eglDisplay_),
                   static_cast<EGLSurface>(eglSurface_));
  }
#endif
}

RenderedFrame OpenGLContext::readDefaultFramebuffer(int width, int height)
{
  RenderedFrame frame;
  frame.width = width;
  frame.height = height;
  if (frame.width <= 0 || frame.height <= 0) {
    return frame;
  }

  frame.format = RenderedFrameFormat::RGBA8;
  frame.pixels.resize(static_cast<size_t>(frame.width) *
                      static_cast<size_t>(frame.height) * 4);

  GLint prevPackAlignment = 4;
  glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlignment);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(headless_ ? GL_FRONT : GL_BACK);
  glReadPixels(0,
               0,
               frame.width,
               frame.height,
               GL_RGBA,
               GL_UNSIGNED_BYTE,
               frame.pixels.data());
  glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlignment);

  const size_t stride = static_cast<size_t>(frame.width) * 4;
  std::vector<uint8_t> row(stride);
  for (int y = 0; y < frame.height / 2; ++y) {
    uint8_t* top = frame.pixels.data() + static_cast<size_t>(y) * stride;
    uint8_t* bottom =
      frame.pixels.data() + static_cast<size_t>(frame.height - 1 - y) * stride;
    std::copy(top, top + stride, row.data());
    std::copy(bottom, bottom + stride, top);
    std::copy(row.data(), row.data() + stride, bottom);
  }

  return frame;
}

std::unique_ptr<GraphicsContext> CreateDefaultGraphicsContext()
{
  return std::make_unique<OpenGLContext>();
}
