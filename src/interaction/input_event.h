#pragma once

#include <mutex>
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
