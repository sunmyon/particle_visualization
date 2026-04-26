#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

using GlGetStringFn = const unsigned char* (*)(unsigned int);

constexpr unsigned int GL_VENDOR = 0x1F00;
constexpr unsigned int GL_RENDERER = 0x1F01;
constexpr unsigned int GL_VERSION = 0x1F02;

struct TestCase {
  std::string displayKind;
  std::string api;
  std::string contextKind;
};

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
            << std::hex << eglGetError() << std::dec << '\n';
}

EGLDisplay GetDisplay(const std::string& kind)
{
  if (kind == "default") {
    std::cerr << "  eglGetDisplay(EGL_DEFAULT_DISPLAY)\n";
    return eglGetDisplay(EGL_DEFAULT_DISPLAY);
  }

  auto getPlatformDisplay =
    reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
      eglGetProcAddress("eglGetPlatformDisplayEXT"));
  if (!getPlatformDisplay) {
    std::cerr << "  eglGetPlatformDisplayEXT unavailable\n";
    return EGL_NO_DISPLAY;
  }

  if (kind == "surfaceless") {
    std::cerr << "  eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA)\n";
    return getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                              EGL_DEFAULT_DISPLAY,
                              nullptr);
  }

  if (kind == "device0") {
    auto queryDevices =
      reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(
        eglGetProcAddress("eglQueryDevicesEXT"));
    if (!queryDevices) {
      std::cerr << "  eglQueryDevicesEXT unavailable\n";
      return EGL_NO_DISPLAY;
    }

    EGLDeviceEXT devices[16] = {};
    EGLint count = 0;
    if (!queryDevices(16, devices, &count) || count <= 0) {
      PrintEglError("  eglQueryDevicesEXT failed");
      return EGL_NO_DISPLAY;
    }

    std::cerr << "  eglQueryDevicesEXT found " << count << " device(s)\n";
    std::cerr << "  eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, device0)\n";
    return getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devices[0], nullptr);
  }

  return EGL_NO_DISPLAY;
}

int RunOne(const TestCase& test)
{
  std::cerr << "CASE display=" << test.displayKind
            << " api=" << test.api
            << " context=" << test.contextKind << '\n';

  EGLDisplay display = GetDisplay(test.displayKind);
  if (display == EGL_NO_DISPLAY) {
    PrintEglError("  no display");
    return 2;
  }

  EGLint major = 0;
  EGLint minor = 0;
  std::cerr << "  eglInitialize\n";
  if (!eglInitialize(display, &major, &minor)) {
    PrintEglError("  eglInitialize failed");
    return 3;
  }

  std::cerr << "  EGL version " << major << "." << minor << '\n';
  std::cerr << "  EGL vendor: "
            << (eglQueryString(display, EGL_VENDOR)
                ? eglQueryString(display, EGL_VENDOR) : "(null)") << '\n';

  const bool useGles = test.api == "gles";
  const EGLenum api = useGles ? EGL_OPENGL_ES_API : EGL_OPENGL_API;
  const EGLint renderable = useGles ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_BIT;

  std::cerr << "  eglBindAPI(" << (useGles ? "GLES" : "OpenGL") << ")\n";
  if (!eglBindAPI(api)) {
    PrintEglError("  eglBindAPI failed");
    eglTerminate(display);
    return 4;
  }

  const EGLint configAttribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, renderable,
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
  std::cerr << "  eglChooseConfig\n";
  if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs) ||
      numConfigs <= 0) {
    PrintEglError("  eglChooseConfig failed");
    eglTerminate(display);
    return 5;
  }

  const EGLint surfaceAttribs[] = {
    EGL_WIDTH, 64,
    EGL_HEIGHT, 64,
    EGL_NONE
  };
  std::cerr << "  eglCreatePbufferSurface\n";
  EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttribs);
  if (surface == EGL_NO_SURFACE) {
    PrintEglError("  eglCreatePbufferSurface failed");
    eglTerminate(display);
    return 6;
  }

  std::vector<EGLint> contextAttribs;
  if (useGles) {
    contextAttribs = {
      EGL_CONTEXT_CLIENT_VERSION,
      test.contextKind == "strict" ? 3 : 2,
      EGL_NONE
    };
  } else if (test.contextKind == "strict") {
    contextAttribs = {
      EGL_CONTEXT_MAJOR_VERSION, 3,
      EGL_CONTEXT_MINOR_VERSION, 3,
      EGL_NONE
    };
  } else {
    contextAttribs = {EGL_NONE};
  }

  std::cerr << "  eglCreateContext\n";
  EGLContext context =
    eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs.data());
  if (context == EGL_NO_CONTEXT) {
    PrintEglError("  eglCreateContext failed");
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return 7;
  }

  std::cerr << "  eglMakeCurrent\n";
  if (!eglMakeCurrent(display, surface, surface, context)) {
    PrintEglError("  eglMakeCurrent failed");
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return 8;
  }

  auto glGetStringPtr =
    reinterpret_cast<GlGetStringFn>(eglGetProcAddress("glGetString"));
  if (glGetStringPtr) {
    const unsigned char* vendor = glGetStringPtr(GL_VENDOR);
    const unsigned char* renderer = glGetStringPtr(GL_RENDERER);
    const unsigned char* version = glGetStringPtr(GL_VERSION);
    std::cerr << "  GL vendor: " << (vendor ? reinterpret_cast<const char*>(vendor) : "(null)") << '\n';
    std::cerr << "  GL renderer: " << (renderer ? reinterpret_cast<const char*>(renderer) : "(null)") << '\n';
    std::cerr << "  GL version: " << (version ? reinterpret_cast<const char*>(version) : "(null)") << '\n';
  }

  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display, context);
  eglDestroySurface(display, surface);
  eglTerminate(display);
  return 0;
}

void RunCaseInChild(const TestCase& test)
{
  pid_t pid = fork();
  if (pid == 0) {
    std::exit(RunOne(test));
  }
  if (pid < 0) {
    std::cerr << "fork failed\n";
    return;
  }

  int status = 0;
  waitpid(pid, &status, 0);
  if (WIFEXITED(status)) {
    std::cerr << "RESULT exit=" << WEXITSTATUS(status) << "\n\n";
  } else if (WIFSIGNALED(status)) {
    std::cerr << "RESULT signal=" << WTERMSIG(status)
              << " (" << strsignal(WTERMSIG(status)) << ")\n\n";
  } else {
    std::cerr << "RESULT unknown\n\n";
  }
}

} // namespace

int main()
{
  const char* clientExtensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
  std::cerr << "EGL client extensions: "
            << (clientExtensions ? clientExtensions : "(null)") << "\n\n";

  std::vector<TestCase> tests;

  const bool hasDevice =
    HasExtension(clientExtensions, "EGL_EXT_platform_device") &&
    (HasExtension(clientExtensions, "EGL_EXT_device_enumeration") ||
     HasExtension(clientExtensions, "EGL_EXT_device_base"));
  const bool hasSurfaceless =
    HasExtension(clientExtensions, "EGL_MESA_platform_surfaceless");

  if (hasDevice) {
    tests.push_back({"device0", "gl", "default"});
    tests.push_back({"device0", "gl", "strict"});
    tests.push_back({"device0", "gles", "default"});
    tests.push_back({"device0", "gles", "strict"});
  }
  if (hasSurfaceless) {
    tests.push_back({"surfaceless", "gl", "default"});
    tests.push_back({"surfaceless", "gl", "strict"});
    tests.push_back({"surfaceless", "gles", "default"});
    tests.push_back({"surfaceless", "gles", "strict"});
  }
  tests.push_back({"default", "gl", "default"});
  tests.push_back({"default", "gles", "default"});

  for (const TestCase& test : tests) {
    RunCaseInChild(test);
  }

  return 0;
}
