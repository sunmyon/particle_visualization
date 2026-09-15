#pragma once

#include <iterator>
#include <mutex>
#include <string>
#include <vector>

enum class InputEventType {
  PointerMove,
  PointerScroll,
  Key,
  FramebufferResize,
  PointerButton,
  Text
};

enum class PointerButton {
  None,
  Left,
  Right,
  Middle
};

enum class InputKey {
  Unknown,
  Escape,
  Tab,
  Enter,
  Backspace,
  Insert,
  Delete,
  Space,
  Left,
  Right,
  Up,
  Down,
  PageUp,
  PageDown,
  Home,
  End,
  CapsLock,
  ScrollLock,
  NumLock,
  PrintScreen,
  Pause,
  LeftShift,
  RightShift,
  LeftCtrl,
  RightCtrl,
  LeftAlt,
  RightAlt,
  LeftSuper,
  RightSuper,
  Menu,
  A,
  B,
  C,
  D,
  E,
  F,
  G,
  H,
  I,
  J,
  K,
  L,
  M,
  N,
  O,
  P,
  Q,
  R,
  S,
  T,
  U,
  V,
  W,
  X,
  Y,
  Z,
  Digit0,
  Digit1,
  Digit2,
  Digit3,
  Digit4,
  Digit5,
  Digit6,
  Digit7,
  Digit8,
  Digit9,
  F1,
  F2,
  F3,
  F4,
  F5,
  F6,
  F7,
  F8,
  F9,
  F10,
  F11,
  F12,
  F13,
  F14,
  F15,
  F16,
  F17,
  F18,
  F19,
  F20,
  F21,
  F22,
  F23,
  F24,
  Apostrophe,
  Comma,
  Minus,
  Period,
  Slash,
  Semicolon,
  Equal,
  LeftBracket,
  Backslash,
  RightBracket,
  GraveAccent,
  Keypad0,
  Keypad1,
  Keypad2,
  Keypad3,
  Keypad4,
  Keypad5,
  Keypad6,
  Keypad7,
  Keypad8,
  Keypad9,
  KeypadDecimal,
  KeypadDivide,
  KeypadMultiply,
  KeypadSubtract,
  KeypadAdd,
  KeypadEnter,
  KeypadEqual
};

enum class InputAction {
  Press,
  Release,
  Repeat
};

struct InputModifiers {
  bool shift = false;
  bool ctrl = false;
  bool alt = false;
  bool super = false;
};

struct InputViewport {
  int x = 0;
  int y = 0;
  int width = 1;
  int height = 1;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;
};

enum class InputSource { Local, Remote };

struct InputEvent {
  InputEventType type = InputEventType::PointerMove;

  float x = 0.0f;
  float y = 0.0f;
  float wheelX = 0.0f;
  float wheelY = 0.0f;

  int width = 0;
  int height = 0;
  int displayWidth = 0;
  int displayHeight = 0;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;
  bool idlePresentation = false;
  InputKey key = InputKey::Unknown;
  InputAction action = InputAction::Press;

  PointerButton button = PointerButton::None;
  bool primaryDown = false;
  bool capturedByUI = false;

  InputModifiers modifiers;
  InputViewport viewport;

  // Committed UTF-8 text is separate from physical key transitions.
  std::string text;
  InputSource source = InputSource::Local;
};

struct InputEventQueue {
  void push(const InputEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    events.push_back(event);
  }

  std::vector<InputEvent> drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<InputEvent> out;
    out.swap(events);
    return out;
  }

  void prepend(std::vector<InputEvent> pending) {
    if (pending.empty()) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    events.insert(events.begin(),
                  std::make_move_iterator(pending.begin()),
                  std::make_move_iterator(pending.end()));
  }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    events.clear();
  }

  bool empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events.empty();
  }

private:
  mutable std::mutex mutex_;
  std::vector<InputEvent> events;
};
