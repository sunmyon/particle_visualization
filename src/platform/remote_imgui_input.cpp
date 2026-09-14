#include "platform/remote_imgui_input.h"

#include <array>
#include <cstddef>

#include <imgui.h>

namespace {

constexpr std::array<ImGuiKey,
  static_cast<std::size_t>(InputKey::KeypadEqual) + 1> InputKeyMap = {
  ImGuiKey_None,
  ImGuiKey_Escape,
  ImGuiKey_Tab,
  ImGuiKey_Enter,
  ImGuiKey_Backspace,
  ImGuiKey_Insert,
  ImGuiKey_Delete,
  ImGuiKey_Space,
  ImGuiKey_LeftArrow,
  ImGuiKey_RightArrow,
  ImGuiKey_UpArrow,
  ImGuiKey_DownArrow,
  ImGuiKey_PageUp,
  ImGuiKey_PageDown,
  ImGuiKey_Home,
  ImGuiKey_End,
  ImGuiKey_CapsLock,
  ImGuiKey_ScrollLock,
  ImGuiKey_NumLock,
  ImGuiKey_PrintScreen,
  ImGuiKey_Pause,
  ImGuiKey_LeftShift,
  ImGuiKey_RightShift,
  ImGuiKey_LeftCtrl,
  ImGuiKey_RightCtrl,
  ImGuiKey_LeftAlt,
  ImGuiKey_RightAlt,
  ImGuiKey_LeftSuper,
  ImGuiKey_RightSuper,
  ImGuiKey_Menu,
  ImGuiKey_A,
  ImGuiKey_B,
  ImGuiKey_C,
  ImGuiKey_D,
  ImGuiKey_E,
  ImGuiKey_F,
  ImGuiKey_G,
  ImGuiKey_H,
  ImGuiKey_I,
  ImGuiKey_J,
  ImGuiKey_K,
  ImGuiKey_L,
  ImGuiKey_M,
  ImGuiKey_N,
  ImGuiKey_O,
  ImGuiKey_P,
  ImGuiKey_Q,
  ImGuiKey_R,
  ImGuiKey_S,
  ImGuiKey_T,
  ImGuiKey_U,
  ImGuiKey_V,
  ImGuiKey_W,
  ImGuiKey_X,
  ImGuiKey_Y,
  ImGuiKey_Z,
  ImGuiKey_0,
  ImGuiKey_1,
  ImGuiKey_2,
  ImGuiKey_3,
  ImGuiKey_4,
  ImGuiKey_5,
  ImGuiKey_6,
  ImGuiKey_7,
  ImGuiKey_8,
  ImGuiKey_9,
  ImGuiKey_F1,
  ImGuiKey_F2,
  ImGuiKey_F3,
  ImGuiKey_F4,
  ImGuiKey_F5,
  ImGuiKey_F6,
  ImGuiKey_F7,
  ImGuiKey_F8,
  ImGuiKey_F9,
  ImGuiKey_F10,
  ImGuiKey_F11,
  ImGuiKey_F12,
  ImGuiKey_F13,
  ImGuiKey_F14,
  ImGuiKey_F15,
  ImGuiKey_F16,
  ImGuiKey_F17,
  ImGuiKey_F18,
  ImGuiKey_F19,
  ImGuiKey_F20,
  ImGuiKey_F21,
  ImGuiKey_F22,
  ImGuiKey_F23,
  ImGuiKey_F24,
  ImGuiKey_Apostrophe,
  ImGuiKey_Comma,
  ImGuiKey_Minus,
  ImGuiKey_Period,
  ImGuiKey_Slash,
  ImGuiKey_Semicolon,
  ImGuiKey_Equal,
  ImGuiKey_LeftBracket,
  ImGuiKey_Backslash,
  ImGuiKey_RightBracket,
  ImGuiKey_GraveAccent,
  ImGuiKey_Keypad0,
  ImGuiKey_Keypad1,
  ImGuiKey_Keypad2,
  ImGuiKey_Keypad3,
  ImGuiKey_Keypad4,
  ImGuiKey_Keypad5,
  ImGuiKey_Keypad6,
  ImGuiKey_Keypad7,
  ImGuiKey_Keypad8,
  ImGuiKey_Keypad9,
  ImGuiKey_KeypadDecimal,
  ImGuiKey_KeypadDivide,
  ImGuiKey_KeypadMultiply,
  ImGuiKey_KeypadSubtract,
  ImGuiKey_KeypadAdd,
  ImGuiKey_KeypadEnter,
  ImGuiKey_KeypadEqual
};

ImGuiKey ToImGuiKey(InputKey key)
{
  const std::size_t index = static_cast<std::size_t>(key);
  return index < InputKeyMap.size() ? InputKeyMap[index] : ImGuiKey_None;
}

int ToImGuiMouseButton(PointerButton button)
{
  switch (button) {
  case PointerButton::Left: return ImGuiMouseButton_Left;
  case PointerButton::Right: return ImGuiMouseButton_Right;
  case PointerButton::Middle: return ImGuiMouseButton_Middle;
  case PointerButton::None: return -1;
  }
  return -1;
}

bool IsMouseEvent(InputEventType type)
{
  return type == InputEventType::PointerMove ||
         type == InputEventType::PointerButton ||
         type == InputEventType::PointerScroll;
}

bool IsKeyboardEvent(InputEventType type)
{
  return type == InputEventType::Key || type == InputEventType::Text;
}

} // namespace

void QueueRemoteInputForImGui(const std::vector<InputEvent>& events)
{
  if (!ImGui::GetCurrentContext()) {
    return;
  }

  ImGuiIO& io = ImGui::GetIO();
  for (const InputEvent& event : events) {
    if (event.source != InputSource::Remote) {
      continue;
    }

    switch (event.type) {
    case InputEventType::PointerMove:
      io.AddMousePosEvent(event.x, event.y);
      break;
    case InputEventType::PointerButton: {
      io.AddMousePosEvent(event.x, event.y);
      const int button = ToImGuiMouseButton(event.button);
      if (button >= 0 && event.action != InputAction::Repeat) {
        io.AddMouseButtonEvent(button, event.action == InputAction::Press);
      }
      break;
    }
    case InputEventType::PointerScroll:
      io.AddMousePosEvent(event.x, event.y);
      io.AddMouseWheelEvent(event.wheelX, event.wheelY);
      break;
    case InputEventType::Key: {
      io.AddKeyEvent(ImGuiMod_Shift, event.modifiers.shift);
      io.AddKeyEvent(ImGuiMod_Ctrl, event.modifiers.ctrl);
      io.AddKeyEvent(ImGuiMod_Alt, event.modifiers.alt);
      io.AddKeyEvent(ImGuiMod_Super, event.modifiers.super);
      // ImGui generates key repeat internally from press/release state.
      if (event.action == InputAction::Repeat) {
        break;
      }
      const ImGuiKey key = ToImGuiKey(event.key);
      if (key != ImGuiKey_None) {
        io.AddKeyEvent(key, event.action == InputAction::Press);
      }
      break;
    }
    case InputEventType::Text:
      io.AddInputCharactersUTF8(event.text.c_str());
      break;
    case InputEventType::FramebufferResize:
      break;
    }
  }
}

void ApplyRemoteInputCapture(std::vector<InputEvent>& events,
                             const RemoteInputCapture& capture)
{
  for (InputEvent& event : events) {
    if (event.source != InputSource::Remote) {
      continue;
    }
    if (IsMouseEvent(event.type)) {
      event.capturedByUI = capture.mouse;
    } else if (IsKeyboardEvent(event.type)) {
      event.capturedByUI = capture.keyboard;
    }
  }
}

RemoteInputCapture CurrentImGuiInputCapture()
{
  if (!ImGui::GetCurrentContext()) {
    return {};
  }
  const ImGuiIO& io = ImGui::GetIO();
  return {io.WantCaptureMouse, io.WantCaptureKeyboard};
}
