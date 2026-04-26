#include "imgui_context.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "implot.h"

#include <GLFW/glfw3.h>

namespace {
bool g_useGlfwBackend = false;
}

void InitImGuiContext(GLFWwindow* window)
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  (void)io;

  ImGui::StyleColorsDark();
  ImPlot::CreateContext();

  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");
  g_useGlfwBackend = true;
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
  if (g_useGlfwBackend) {
    ImGui_ImplGlfw_NewFrame();
  } else {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width),
                            static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
  }
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
  if (g_useGlfwBackend) {
    ImGui_ImplGlfw_Shutdown();
  }
  g_useGlfwBackend = false;
  ImGui::DestroyContext();
  ImPlot::DestroyContext();
}
