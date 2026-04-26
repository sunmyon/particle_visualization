#pragma once

#include <vector>

enum class InputEventType {
  PointerMove,
  PointerScroll,
  Key,
  FramebufferResize
};

enum class PointerButton {
  None,
  Left,
  Right,
  Middle
};

enum class InputKey {
  Unknown,
  Escape
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

struct InputEvent {
  InputEventType type = InputEventType::PointerMove;

  float x = 0.0f;
  float y = 0.0f;
  float wheelX = 0.0f;
  float wheelY = 0.0f;

  int width = 0;
  int height = 0;
  InputKey key = InputKey::Unknown;
  InputAction action = InputAction::Press;

  PointerButton button = PointerButton::None;
  bool primaryDown = false;
  bool capturedByUI = false;

  InputModifiers modifiers;
  InputViewport viewport;
};

struct InputEventQueue {
  std::vector<InputEvent> events;

  void push(const InputEvent& event) {
    events.push_back(event);
  }

  void clear() {
    events.clear();
  }

  bool empty() const {
    return events.empty();
  }
};
