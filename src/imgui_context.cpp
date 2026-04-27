#include "imgui_context.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include "implot.h"
#include "window_context.h"

#ifndef PARTICLE_VIS_HEADLESS_ONLY
#include <imgui_impl_glfw.h>
#include <GLFW/glfw3.h>
#endif

namespace {
bool g_useGlfwBackend = false;
}

void InitImGuiContext(WindowContext& window)
{
#ifdef PARTICLE_VIS_HEADLESS_ONLY
  (void)window;
  InitHeadlessImGuiContext(1280, 720);
#else
  auto* nativeWindow = static_cast<GLFWwindow*>(window.nativeWindowHandle());
  if (!nativeWindow) {
    InitHeadlessImGuiContext(window.framebufferWidth(),
                             window.framebufferHeight());
    return;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  (void)io;

  ImGui::StyleColorsDark();
  ImPlot::CreateContext();

  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

  ImGui_ImplGlfw_InitForOpenGL(nativeWindow, true);
  ImGui_ImplOpenGL3_Init("#version 330");
  g_useGlfwBackend = true;
#endif
}

void InitHeadlessImGuiContext(int width, int height)
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(width),
                          static_cast<float>(height));
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

  ImGui::StyleColorsDark();
  ImPlot::CreateContext();

  ImGui_ImplOpenGL3_Init("#version 330");
  g_useGlfwBackend = false;
}

void BeginImGuiFrame(int width, int height)
{
  ImGui_ImplOpenGL3_NewFrame();
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (g_useGlfwBackend) {
    ImGui_ImplGlfw_NewFrame();
  } else {
#endif
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width),
                            static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  }
#endif
  ImGui::NewFrame();
}

void EndImGuiFrame()
{
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ShutdownImGuiContext()
{
  ImGui_ImplOpenGL3_Shutdown();
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (g_useGlfwBackend) {
    ImGui_ImplGlfw_Shutdown();
  }
#endif
  g_useGlfwBackend = false;
  ImGui::DestroyContext();
  ImPlot::DestroyContext();
}
