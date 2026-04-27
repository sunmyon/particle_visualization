#include "app_callbacks.h"

#ifndef PARTICLE_VIS_HEADLESS_ONLY
#include "app/state/app_state.h"
#include "window_context.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>

namespace {

CallbackContext* GetCallbackContext(GLFWwindow* window)
{
  return static_cast<CallbackContext*>(glfwGetWindowUserPointer(window));
}

InputModifiers ReadModifiers(GLFWwindow* window)
{
  InputModifiers modifiers;
  modifiers.shift =
    (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
     glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
  modifiers.ctrl =
    (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
     glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
  modifiers.alt =
    (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
     glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
  modifiers.super =
    (glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
     glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS);
  return modifiers;
}

InputViewport MakeViewport(const WindowContext& window)
{
  InputViewport viewport;
  viewport.x = window.viewportX();
  viewport.y = window.viewportY();
  viewport.width = window.viewportWidth();
  viewport.height = window.viewportHeight();

  const ImGuiIO& io = ImGui::GetIO();
  viewport.framebufferScaleX = io.DisplayFramebufferScale.x;
  viewport.framebufferScaleY = io.DisplayFramebufferScale.y;
  return viewport;
}

InputKey MapKey(int key)
{
  if (key == GLFW_KEY_ESCAPE) {
    return InputKey::Escape;
  }
  return InputKey::Unknown;
}

InputAction MapAction(int action)
{
  if (action == GLFW_RELEASE) {
    return InputAction::Release;
  }
  if (action == GLFW_REPEAT) {
    return InputAction::Repeat;
  }
  return InputAction::Press;
}

} // namespace

static void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
  CallbackContext* ctx = GetCallbackContext(window);
  if (!ctx || !ctx->window)
    return;

  ctx->window->updateFramebufferSize(width, height);

  if (ctx->app) {
    InputEvent event;
    event.type = InputEventType::FramebufferResize;
    event.width = width;
    event.height = height;
    event.viewport = MakeViewport(*ctx->window);
    ctx->app->runtime.inputEvents.push(event);
  }
}

static void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
  CallbackContext* ctx = GetCallbackContext(window);
  if (!ctx || !ctx->app || !ctx->window)
    return;

  AppState& app = *ctx->app;

  InputEvent event;
  event.type = InputEventType::PointerMove;
  event.x = static_cast<float>(xpos);
  event.y = static_cast<float>(ypos);
  event.button = PointerButton::Left;
  event.primaryDown =
    (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
  event.capturedByUI = ImGui::GetIO().WantCaptureMouse;
  event.modifiers = ReadModifiers(window);
  event.viewport = MakeViewport(*ctx->window);
  app.runtime.inputEvents.push(event);
}

static void scroll_callback(GLFWwindow* window, double /*xoffset*/, double yoffset)
{
  CallbackContext* ctx = GetCallbackContext(window);
  if (!ctx || !ctx->app || !ctx->window)
    return;

  AppState& app = *ctx->app;

  InputEvent event;
  event.type = InputEventType::PointerScroll;
  event.wheelY = static_cast<float>(yoffset);
  event.capturedByUI = ImGui::GetIO().WantCaptureMouse;
  event.modifiers = ReadModifiers(window);
  event.viewport = MakeViewport(*ctx->window);
  app.runtime.inputEvents.push(event);
}

static void key_callback(GLFWwindow* window,
                         int key,
                         int /*scancode*/,
                         int action,
                         int /*mods*/)
{
  CallbackContext* ctx = GetCallbackContext(window);
  if (!ctx || !ctx->app || !ctx->window)
    return;

  InputEvent event;
  event.type = InputEventType::Key;
  event.key = MapKey(key);
  event.action = MapAction(action);
  event.capturedByUI = ImGui::GetIO().WantCaptureKeyboard;
  event.modifiers = ReadModifiers(window);
  event.viewport = MakeViewport(*ctx->window);
  ctx->app->runtime.inputEvents.push(event);
}

void AttachAppCallbacks(WindowContext& window, CallbackContext& callbackCtx)
{
  auto* handle = static_cast<GLFWwindow*>(window.nativeWindowHandle());
  if (!handle) {
    return;
  }

  glfwSetWindowUserPointer(handle, &callbackCtx);
  glfwSetCursorPosCallback(handle, mouse_callback);
  glfwSetScrollCallback(handle, scroll_callback);
  glfwSetKeyCallback(handle, key_callback);
  glfwSetFramebufferSizeCallback(handle, framebuffer_size_callback);
}
#else
void AttachAppCallbacks(WindowContext& window, CallbackContext& callbackCtx)
{
  (void)window;
  (void)callbackCtx;
}
#endif
