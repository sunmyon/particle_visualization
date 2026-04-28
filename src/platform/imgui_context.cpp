#include "platform/imgui_context.h"

#include "platform/window_backend.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include "implot.h"

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
