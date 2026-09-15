#include "platform/remote_input_protocol.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace RemoteInputProtocol {
namespace {
using Json = nlohmann::json;

std::optional<InputKey> ParseKey(const std::string& name)
{
  // The prototype viewer sends an empty name for unrecognized keys.
  if (name.empty() || name == "Unknown") return InputKey::Unknown;
  static const std::pair<const char*, InputKey> keys[] = {
    {"Escape", InputKey::Escape},
    {"Tab", InputKey::Tab},
    {"Enter", InputKey::Enter},
    {"Backspace", InputKey::Backspace},
    {"Insert", InputKey::Insert},
    {"Delete", InputKey::Delete},
    {"Space", InputKey::Space},
    {"Left", InputKey::Left},
    {"Right", InputKey::Right},
    {"Up", InputKey::Up},
    {"Down", InputKey::Down},
    {"PageUp", InputKey::PageUp},
    {"PageDown", InputKey::PageDown},
    {"Home", InputKey::Home},
    {"End", InputKey::End},
    {"CapsLock", InputKey::CapsLock},
    {"ScrollLock", InputKey::ScrollLock},
    {"NumLock", InputKey::NumLock},
    {"PrintScreen", InputKey::PrintScreen},
    {"Pause", InputKey::Pause},
    {"LeftShift", InputKey::LeftShift},
    {"RightShift", InputKey::RightShift},
    {"LeftCtrl", InputKey::LeftCtrl},
    {"RightCtrl", InputKey::RightCtrl},
    {"LeftAlt", InputKey::LeftAlt},
    {"RightAlt", InputKey::RightAlt},
    {"LeftSuper", InputKey::LeftSuper},
    {"RightSuper", InputKey::RightSuper},
    {"Menu", InputKey::Menu},
    {"A", InputKey::A},
    {"B", InputKey::B},
    {"C", InputKey::C},
    {"D", InputKey::D},
    {"E", InputKey::E},
    {"F", InputKey::F},
    {"G", InputKey::G},
    {"H", InputKey::H},
    {"I", InputKey::I},
    {"J", InputKey::J},
    {"K", InputKey::K},
    {"L", InputKey::L},
    {"M", InputKey::M},
    {"N", InputKey::N},
    {"O", InputKey::O},
    {"P", InputKey::P},
    {"Q", InputKey::Q},
    {"R", InputKey::R},
    {"S", InputKey::S},
    {"T", InputKey::T},
    {"U", InputKey::U},
    {"V", InputKey::V},
    {"W", InputKey::W},
    {"X", InputKey::X},
    {"Y", InputKey::Y},
    {"Z", InputKey::Z},
    {"Digit0", InputKey::Digit0},
    {"Digit1", InputKey::Digit1},
    {"Digit2", InputKey::Digit2},
    {"Digit3", InputKey::Digit3},
    {"Digit4", InputKey::Digit4},
    {"Digit5", InputKey::Digit5},
    {"Digit6", InputKey::Digit6},
    {"Digit7", InputKey::Digit7},
    {"Digit8", InputKey::Digit8},
    {"Digit9", InputKey::Digit9},
    {"F1", InputKey::F1},
    {"F2", InputKey::F2},
    {"F3", InputKey::F3},
    {"F4", InputKey::F4},
    {"F5", InputKey::F5},
    {"F6", InputKey::F6},
    {"F7", InputKey::F7},
    {"F8", InputKey::F8},
    {"F9", InputKey::F9},
    {"F10", InputKey::F10},
    {"F11", InputKey::F11},
    {"F12", InputKey::F12},
    {"F13", InputKey::F13},
    {"F14", InputKey::F14},
    {"F15", InputKey::F15},
    {"F16", InputKey::F16},
    {"F17", InputKey::F17},
    {"F18", InputKey::F18},
    {"F19", InputKey::F19},
    {"F20", InputKey::F20},
    {"F21", InputKey::F21},
    {"F22", InputKey::F22},
    {"F23", InputKey::F23},
    {"F24", InputKey::F24},
    {"Apostrophe", InputKey::Apostrophe},
    {"Comma", InputKey::Comma},
    {"Minus", InputKey::Minus},
    {"Period", InputKey::Period},
    {"Slash", InputKey::Slash},
    {"Semicolon", InputKey::Semicolon},
    {"Equal", InputKey::Equal},
    {"LeftBracket", InputKey::LeftBracket},
    {"Backslash", InputKey::Backslash},
    {"RightBracket", InputKey::RightBracket},
    {"GraveAccent", InputKey::GraveAccent},
    {"Keypad0", InputKey::Keypad0},
    {"Keypad1", InputKey::Keypad1},
    {"Keypad2", InputKey::Keypad2},
    {"Keypad3", InputKey::Keypad3},
    {"Keypad4", InputKey::Keypad4},
    {"Keypad5", InputKey::Keypad5},
    {"Keypad6", InputKey::Keypad6},
    {"Keypad7", InputKey::Keypad7},
    {"Keypad8", InputKey::Keypad8},
    {"Keypad9", InputKey::Keypad9},
    {"KeypadDecimal", InputKey::KeypadDecimal},
    {"KeypadDivide", InputKey::KeypadDivide},
    {"KeypadMultiply", InputKey::KeypadMultiply},
    {"KeypadSubtract", InputKey::KeypadSubtract},
    {"KeypadAdd", InputKey::KeypadAdd},
    {"KeypadEnter", InputKey::KeypadEnter},
    {"KeypadEqual", InputKey::KeypadEqual},
  };
  for (const auto& key : keys) {
    if (name == key.first) return key.second;
  }
  return std::nullopt;
}

bool ReadInt(const Json& json, const char* name, int& value)
{
  const auto it = json.find(name);
  if (it == json.end()) return true;
  if (!it->is_number_integer()) return false;
  if (*it < std::numeric_limits<int>::min() ||
      *it > std::numeric_limits<int>::max()) return false;
  value = it->get<int>();
  return true;
}

bool ReadFloat(const Json& json, const char* name, float& value)
{
  const auto it = json.find(name);
  if (it == json.end()) return true;
  if (!it->is_number()) return false;
  const double number = it->get<double>();
  if (!std::isfinite(number) ||
      std::abs(number) > std::numeric_limits<float>::max()) return false;
  value = static_cast<float>(number);
  return true;
}
} // namespace

std::optional<InputEvent> Decode(std::string_view message)
{
  if (message.empty() || message.size() > MaxMessageBytes) return std::nullopt;
  try {
    const Json json = Json::parse(message.begin(), message.end());
    if (!json.is_object()) return std::nullopt;
    int version = 1;
    if (!ReadInt(json, "version", version) || version != 1) return std::nullopt;

    InputEvent event;
    event.source = InputSource::Remote;
    const auto type = json.at("type").get<std::string>();
    if (type == "pointer_move") event.type = InputEventType::PointerMove;
    else if (type == "pointer_scroll") event.type = InputEventType::PointerScroll;
    else if (type == "pointer_button") event.type = InputEventType::PointerButton;
    else if (type == "key") event.type = InputEventType::Key;
    else if (type == "text") event.type = InputEventType::Text;
    else if (type == "framebuffer_resize") event.type = InputEventType::FramebufferResize;
    else return std::nullopt;

    if (!ReadFloat(json, "x", event.x) || !ReadFloat(json, "y", event.y) ||
        !ReadFloat(json, "wheelX", event.wheelX) ||
        !ReadFloat(json, "wheelY", event.wheelY) ||
        !ReadInt(json, "width", event.width) ||
        !ReadInt(json, "height", event.height) ||
        !ReadInt(json, "displayWidth", event.displayWidth) ||
        !ReadInt(json, "displayHeight", event.displayHeight) ||
        !ReadFloat(json, "framebufferScaleX", event.framebufferScaleX) ||
        !ReadFloat(json, "framebufferScaleY", event.framebufferScaleY))
      return std::nullopt;

    const auto action = json.value("action", std::string("Press"));
    if (action == "Press") event.action = InputAction::Press;
    else if (action == "Release") event.action = InputAction::Release;
    else if (action == "Repeat") event.action = InputAction::Repeat;
    else return std::nullopt;

    const auto key = ParseKey(json.value("key", std::string()));
    if (!key) return std::nullopt;
    event.key = *key;
    const auto button = json.value("button", std::string("None"));
    if (button == "None") event.button = PointerButton::None;
    else if (button == "Left") event.button = PointerButton::Left;
    else if (button == "Right") event.button = PointerButton::Right;
    else if (button == "Middle") event.button = PointerButton::Middle;
    else return std::nullopt;

    event.primaryDown = json.value("primaryDown", false);
    event.capturedByUI = json.value("capturedByUI", false);
    event.deferFrame = json.value("deferFrame", false);
    event.text = json.value("text", std::string());

    if (json.contains("modifiers")) {
      const auto& m = json.at("modifiers");
      if (!m.is_object()) return std::nullopt;
      event.modifiers.shift = m.value("shift", false);
      event.modifiers.ctrl = m.value("ctrl", false);
      event.modifiers.alt = m.value("alt", false);
      event.modifiers.super = m.value("super", false);
    }
    if (json.contains("viewport")) {
      const auto& v = json.at("viewport");
      if (!v.is_object() ||
          !ReadInt(v, "x", event.viewport.x) ||
          !ReadInt(v, "y", event.viewport.y) ||
          !ReadInt(v, "width", event.viewport.width) ||
          !ReadInt(v, "height", event.viewport.height) ||
          !ReadFloat(v, "framebufferScaleX", event.viewport.framebufferScaleX) ||
          !ReadFloat(v, "framebufferScaleY", event.viewport.framebufferScaleY))
        return std::nullopt;
      if (event.viewport.width <= 0 || event.viewport.height <= 0 ||
          event.viewport.framebufferScaleX <= 0 ||
          event.viewport.framebufferScaleY <= 0) return std::nullopt;
    }

    if (event.type == InputEventType::PointerButton &&
        (event.button == PointerButton::None || !json.contains("action") ||
         event.action == InputAction::Repeat)) return std::nullopt;
    if (event.type == InputEventType::Text &&
        (event.text.empty() || event.text.find('\0') != std::string::npos))
      return std::nullopt;
    if (event.type == InputEventType::FramebufferResize) {
      if (event.displayWidth == 0 && event.displayHeight == 0) {
        event.displayWidth = event.width;
        event.displayHeight = event.height;
      }
      if (event.width <= 0 || event.height <= 0 ||
          event.displayWidth <= 0 || event.displayHeight <= 0 ||
          event.width > MaxFramebufferDimension ||
          event.height > MaxFramebufferDimension ||
          event.displayWidth > MaxFramebufferDimension ||
          event.displayHeight > MaxFramebufferDimension ||
          event.framebufferScaleX <= 0.0f ||
          event.framebufferScaleY <= 0.0f ||
          static_cast<std::size_t>(event.width) *
              static_cast<std::size_t>(event.height) > MaxFramebufferPixels)
        return std::nullopt;
    }
    return event;
  } catch (const Json::exception&) {
    // A malformed peer must not unwind the transport's receiving thread.
    return std::nullopt;
  }
}

bool IsFrameRequest(std::string_view message)
{
  if (message.empty() || message.size() > MaxMessageBytes) return false;
  try {
    const Json json = Json::parse(message.begin(), message.end());
    if (!json.is_object() ||
        json.value("type", std::string()) != "frame_request") {
      return false;
    }
    int version = 1;
    return ReadInt(json, "version", version) && version == 1;
  } catch (const Json::exception&) {
    return false;
  }
}
} // namespace RemoteInputProtocol
