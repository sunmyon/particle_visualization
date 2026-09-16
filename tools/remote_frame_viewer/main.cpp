#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>
#include <zmq.hpp>

#include "platform/remote_video_codec.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {

struct RemoteFrame {
  uint64_t frameId = 0;
  uint64_t triggerSequence = 0;
  uint64_t cameraGeneration = 0;
  uint64_t latestCameraGeneration = 0;
  std::size_t outstandingFrames = 0;
  std::size_t applicationQueueDepth = 0;
  uint64_t sendBackpressureCount = 0;
  int64_t serverInputReceivedAtNs = 0;
  int64_t serverCameraUpdatedAtNs = 0;
  int64_t serverRenderStartedAtNs = 0;
  int64_t serverRenderFinishedAtNs = 0;
  int64_t serverEncodeStartedAtNs = 0;
  int64_t serverEncodeFinishedAtNs = 0;
  int64_t serverSendAttemptAtNs = 0;
  int width = 0;
  int height = 0;
  int displayWidth = 0;
  int displayHeight = 0;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;
  std::string encoding = "RGBA8";
  bool idlePresentation = false;
  std::size_t payloadBytes = 0;
  double serverReadbackMs = 0.0;
  double serverReadbackLatencyMs = 0.0;
  double serverTriggerToReadbackMs = 0.0;
  double serverInputToFrameStartMs = 0.0;
  double serverFrameToRenderMs = 0.0;
  double serverRenderMs = 0.0;
  double serverEncoderQueueMs = 0.0;
  double serverEncodeMs = 0.0;
  double serverEncodeToSendMs = 0.0;
  double serverPreviousSendMs = 0.0;
  double clientPayloadReceiveMs = 0.0;
  double clientDecodeMs = 0.0;
  double clientInputToReceiveMs = -1.0;
  std::chrono::steady_clock::time_point receivedAt{};
  std::chrono::steady_clock::time_point inputSentAt{};
  std::vector<uint8_t> rgba;
};

struct ViewerInputContext {
  zmq::socket_t* input = nullptr;
  bool leftDown = false;
  bool rightDown = false;
  bool middleDown = false;
  int remoteWidth = 1280;
  int remoteHeight = 720;
  int remoteDisplayWidth = 1280;
  int remoteDisplayHeight = 720;
  float remoteFramebufferScaleX = 1.0f;
  float remoteFramebufferScaleY = 1.0f;
  float interactiveRenderScale = 0.75f;
  float idleRenderScale = 2.0f;
  int idleRestoreDelayMs = 1500;
  bool interactiveRendering = true;
  std::chrono::steady_clock::time_point lastInteraction =
    std::chrono::steady_clock::now();
  bool initialResizeSent = false;
  uint64_t nextInputSequence = 0;
  std::deque<std::pair<uint64_t, std::chrono::steady_clock::time_point>>
    sentInputs;
  std::unordered_set<int> pressedKeys;
  std::unordered_set<int> locallyHandledKeys;
};

struct PointerPosition {
  float x = 0.0f;
  float y = 0.0f;
};

struct RequestedPresentation {
  int framebufferWidth = 1;
  int framebufferHeight = 1;
  int displayWidth = 1;
  int displayHeight = 1;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;
};

int EnvInt(const char* name, int fallback)
{
  const char* value = std::getenv(name);
  if (!value || value[0] == '\0') return fallback;
  try {
    return std::max(1, std::stoi(value));
  } catch (...) {
    return fallback;
  }
}

float EnvFloat(const char* name, float fallback)
{
  const char* value = std::getenv(name);
  if (!value || value[0] == '\0') return fallback;
  try {
    const float parsed = std::stof(value);
    return std::isfinite(parsed) ? std::clamp(parsed, 0.25f, 2.0f)
                                 : fallback;
  } catch (...) {
    return fallback;
  }
}

RequestedPresentation GetRequestedPresentation(GLFWwindow* window)
{
  RequestedPresentation requested;
  int localFramebufferWidth = 1;
  int localFramebufferHeight = 1;
  glfwGetWindowSize(window,
                    &requested.displayWidth,
                    &requested.displayHeight);
  glfwGetFramebufferSize(window,
                         &localFramebufferWidth,
                         &localFramebufferHeight);
  requested.displayWidth = std::max(requested.displayWidth, 1);
  requested.displayHeight = std::max(requested.displayHeight, 1);
  localFramebufferWidth = std::max(localFramebufferWidth, 1);
  localFramebufferHeight = std::max(localFramebufferHeight, 1);

  const auto* ctx =
    static_cast<ViewerInputContext*>(glfwGetWindowUserPointer(window));
  const float renderScale = ctx
    ? (ctx->interactiveRendering
         ? ctx->interactiveRenderScale
         : ctx->idleRenderScale)
    : 1.0f;
  requested.framebufferWidth = std::clamp(
    static_cast<int>(std::lround(requested.displayWidth * renderScale)),
    1,
    localFramebufferWidth);
  requested.framebufferHeight = std::clamp(
    static_cast<int>(std::lround(requested.displayHeight * renderScale)),
    1,
    localFramebufferHeight);
  // I420/H.264 requires even dimensions. Losing at most one pixel also keeps
  // the JPEG fallback compatible with the same resize request.
  if (requested.framebufferWidth > 1) requested.framebufferWidth &= ~1;
  if (requested.framebufferHeight > 1) requested.framebufferHeight &= ~1;
  requested.framebufferScaleX =
    static_cast<float>(requested.framebufferWidth) /
    static_cast<float>(requested.displayWidth);
  requested.framebufferScaleY =
    static_cast<float>(requested.framebufferHeight) /
    static_cast<float>(requested.displayHeight);
  return requested;
}

const char* VertexShaderSource()
{
  return R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    layout (location = 1) in vec2 aUv;
    out vec2 vUv;
    void main() {
      vUv = aUv;
      gl_Position = vec4(aPos, 0.0, 1.0);
    }
  )";
}

const char* FragmentShaderSource()
{
  return R"(
    #version 330 core
    in vec2 vUv;
    out vec4 FragColor;
    uniform sampler2D uFrame;
    void main() {
      // RenderedFrame rows use a top-left origin, while OpenGL textures use
      // a bottom-left origin.
      FragColor = texture(uFrame, vec2(vUv.x, 1.0 - vUv.y));
    }
  )";
}

GLuint CompileShader(GLenum type, const char* source)
{
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    std::cerr << "Shader compile failed: " << log << '\n';
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

GLuint CreateProgram()
{
  GLuint vs = CompileShader(GL_VERTEX_SHADER, VertexShaderSource());
  GLuint fs = CompileShader(GL_FRAGMENT_SHADER, FragmentShaderSource());
  if (!vs || !fs) {
    glDeleteShader(vs);
    glDeleteShader(fs);
    return 0;
  }

  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);

  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    std::cerr << "Program link failed: " << log << '\n';
    glDeleteProgram(program);
    return 0;
  }

  return program;
}

bool ReceiveFrame(zmq::socket_t& sub,
                  RemoteFrame& out,
                  int desiredWidth,
                  int desiredHeight,
                  RemoteVideoDecoder& videoDecoder)
{
  zmq::message_t headerMsg;
  auto headerResult = sub.recv(headerMsg, zmq::recv_flags::dontwait);
  if (!headerResult) {
    return false;
  }
  const auto headerReceivedAt = std::chrono::steady_clock::now();

  zmq::message_t payloadMsg;
  auto payloadResult = sub.recv(payloadMsg, zmq::recv_flags::none);
  if (!payloadResult) {
    return false;
  }
  const auto payloadReceivedAt = std::chrono::steady_clock::now();

  nlohmann::json header =
    nlohmann::json::parse(headerMsg.to_string(), nullptr, false);
  if (header.is_discarded()) {
    return false;
  }

  const std::string type = header.value("type", "");
  if (type != "rgba_frame" && type != "jpeg_frame" && type != "h264_frame") {
    return false;
  }

  const int width = header.value("width", 0);
  const int height = header.value("height", 0);
  const size_t expected =
    static_cast<size_t>(std::max(width, 0)) *
    static_cast<size_t>(std::max(height, 0)) * 4;
  if (width <= 0 || height <= 0) {
    return false;
  }

  if (type == "rgba_frame" && payloadMsg.size() != expected) return false;

  out.frameId = header.value("frameId", uint64_t{0});
  out.triggerSequence = header.value("triggerSequence", uint64_t{0});
  out.cameraGeneration = header.value("cameraGeneration", out.triggerSequence);
  out.latestCameraGeneration =
    header.value("latestCameraGeneration", out.cameraGeneration);
  out.outstandingFrames = header.value("outstandingFrames", std::size_t{0});
  out.applicationQueueDepth =
    header.value("applicationQueueDepth", std::size_t{0});
  out.sendBackpressureCount =
    header.value("sendBackpressureCount", uint64_t{0});
  out.serverInputReceivedAtNs =
    header.value("serverInputReceivedAtNs", int64_t{0});
  out.serverCameraUpdatedAtNs =
    header.value("serverCameraUpdatedAtNs", int64_t{0});
  out.serverRenderStartedAtNs =
    header.value("serverRenderStartedAtNs", int64_t{0});
  out.serverRenderFinishedAtNs =
    header.value("serverRenderFinishedAtNs", int64_t{0});
  out.serverEncodeStartedAtNs =
    header.value("serverEncodeStartedAtNs", int64_t{0});
  out.serverEncodeFinishedAtNs =
    header.value("serverEncodeFinishedAtNs", int64_t{0});
  out.serverSendAttemptAtNs =
    header.value("serverSendAttemptAtNs", int64_t{0});
  out.width = width;
  out.height = height;
  out.displayWidth = header.value("displayWidth", width);
  out.displayHeight = header.value("displayHeight", height);
  out.framebufferScaleX = header.value("framebufferScaleX", 1.0f);
  out.framebufferScaleY = header.value("framebufferScaleY", 1.0f);
  out.encoding = header.value("format", std::string("RGBA8"));
  out.idlePresentation = header.value("presentationMode", std::string()) == "idle";
  out.payloadBytes = payloadMsg.size();
  out.serverReadbackMs = header.value("serverReadbackMs", 0.0);
  out.serverReadbackLatencyMs =
    header.value("serverReadbackLatencyMs", out.serverReadbackMs);
  out.serverTriggerToReadbackMs =
    header.value("serverTriggerToReadbackMs", 0.0);
  out.serverInputToFrameStartMs =
    header.value("serverInputToFrameStartMs", 0.0);
  out.serverFrameToRenderMs = header.value("serverFrameToRenderMs", 0.0);
  out.serverRenderMs = header.value("serverRenderMs", 0.0);
  out.serverEncoderQueueMs = header.value("serverEncoderQueueMs", 0.0);
  out.serverEncodeMs = header.value("serverEncodeMs", 0.0);
  out.serverEncodeToSendMs = header.value("serverEncodeToSendMs", 0.0);
  out.serverPreviousSendMs = header.value("serverPreviousSendMs", 0.0);
  out.clientPayloadReceiveMs = std::chrono::duration<double, std::milli>(
    payloadReceivedAt - headerReceivedAt).count();
  out.receivedAt = payloadReceivedAt;
  const auto decodeStart = std::chrono::steady_clock::now();
  if (type == "h264_frame") {
    if (!videoDecoder.decode(
          static_cast<const unsigned char*>(payloadMsg.data()),
          payloadMsg.size(), width, height, out.rgba)) {
      return false;
    }
  } else if (type == "jpeg_frame") {
    int decodedWidth = 0;
    int decodedHeight = 0;
    int channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(
      static_cast<const stbi_uc*>(payloadMsg.data()),
      static_cast<int>(payloadMsg.size()),
      &decodedWidth,
      &decodedHeight,
      &channels,
      4);
    if (!decoded || decodedWidth != width || decodedHeight != height) {
      stbi_image_free(decoded);
      return false;
    }
    out.rgba.assign(decoded, decoded + expected);
    stbi_image_free(decoded);
  } else {
    out.rgba.resize(payloadMsg.size());
    std::copy(static_cast<const uint8_t*>(payloadMsg.data()),
              static_cast<const uint8_t*>(payloadMsg.data()) + payloadMsg.size(),
              out.rgba.begin());
  }
  out.clientDecodeMs = std::chrono::duration<double, std::milli>(
    std::chrono::steady_clock::now() - decodeStart).count();
  if (desiredWidth > 0 && desiredHeight > 0 &&
      (width != desiredWidth || height != desiredHeight)) {
    out.rgba.clear();
  }
  return true;
}

nlohmann::json BuildModifiers(GLFWwindow* window)
{
  return {
    {"shift", glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
              glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS},
    {"ctrl", glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
             glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS},
    {"alt", glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS},
    {"super", glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
              glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS}
  };
}

nlohmann::json BuildViewport(GLFWwindow* window)
{
  const RequestedPresentation requested = GetRequestedPresentation(window);
  return {
    {"x", 0},
    {"y", 0},
    {"width", requested.displayWidth},
    {"height", requested.displayHeight},
    {"framebufferScaleX", requested.framebufferScaleX},
    {"framebufferScaleY", requested.framebufferScaleY}
  };
}

PointerPosition MapPointerToRemote(GLFWwindow* window, double x, double y)
{
  const auto* ctx =
    static_cast<ViewerInputContext*>(glfwGetWindowUserPointer(window));
  int windowWidth = 1;
  int windowHeight = 1;
  glfwGetWindowSize(window, &windowWidth, &windowHeight);
  const float remoteWidth = static_cast<float>(
    ctx ? std::max(ctx->remoteDisplayWidth, 1) : std::max(windowWidth, 1));
  const float remoteHeight = static_cast<float>(
    ctx ? std::max(ctx->remoteDisplayHeight, 1) : std::max(windowHeight, 1));
  return {
    std::clamp(static_cast<float>(x) * remoteWidth /
                 static_cast<float>(std::max(windowWidth, 1)),
               0.0f,
               remoteWidth),
    std::clamp(static_cast<float>(y) * remoteHeight /
                 static_cast<float>(std::max(windowHeight, 1)),
               0.0f,
               remoteHeight)
  };
}

void SendInput(GLFWwindow* window, nlohmann::json event)
{
  auto* ctx =
    static_cast<ViewerInputContext*>(glfwGetWindowUserPointer(window));
  if (!ctx || !ctx->input) {
    return;
  }

  if (event.value("type", std::string()) != "frame_request") {
    const uint64_t sequence = ++ctx->nextInputSequence;
    event["clientSequence"] = sequence;
    ctx->sentInputs.emplace_back(sequence, std::chrono::steady_clock::now());
    while (ctx->sentInputs.size() > 4096) {
      ctx->sentInputs.pop_front();
    }
  }
  const std::string text = event.dump();
  try {
    ctx->input->send(zmq::buffer(text), zmq::send_flags::dontwait);
  } catch (const zmq::error_t&) {
  }
}

void SendFrameRequest(GLFWwindow* window, uint64_t receivedFrameId = 0)
{
  nlohmann::json request{{"type", "frame_request"}, {"version", 1}};
  if (receivedFrameId) request["receivedFrameId"] = receivedFrameId;
  SendInput(window, std::move(request));
}

void RecordInputToReceive(ViewerInputContext& ctx, RemoteFrame& frame)
{
  if (frame.triggerSequence == 0 ||
      frame.receivedAt.time_since_epoch().count() == 0) {
    return;
  }
  const auto found = std::find_if(
    ctx.sentInputs.begin(), ctx.sentInputs.end(), [&](const auto& sent) {
      return sent.first == frame.triggerSequence;
    });
  if (found == ctx.sentInputs.end()) {
    return;
  }
  frame.clientInputToReceiveMs = std::chrono::duration<double, std::milli>(
    frame.receivedAt - found->second).count();
  frame.inputSentAt = found->second;
  ctx.sentInputs.erase(ctx.sentInputs.begin(), std::next(found));
}

void SendFramebufferSize(GLFWwindow* window)
{
  const RequestedPresentation requested = GetRequestedPresentation(window);
  auto* ctx =
    static_cast<ViewerInputContext*>(glfwGetWindowUserPointer(window));
  if (ctx) {
    ctx->remoteWidth = requested.framebufferWidth;
    ctx->remoteHeight = requested.framebufferHeight;
    ctx->remoteDisplayWidth = requested.displayWidth;
    ctx->remoteDisplayHeight = requested.displayHeight;
    ctx->remoteFramebufferScaleX = requested.framebufferScaleX;
    ctx->remoteFramebufferScaleY = requested.framebufferScaleY;
  }
  SendInput(window, {
    {"type", "framebuffer_resize"},
    {"width", requested.framebufferWidth},
    {"height", requested.framebufferHeight},
    {"displayWidth", requested.displayWidth},
    {"displayHeight", requested.displayHeight},
    {"framebufferScaleX", requested.framebufferScaleX},
    {"framebufferScaleY", requested.framebufferScaleY},
    {"presentationMode", ctx && !ctx->interactiveRendering
                           ? "idle"
                           : "interactive"}
  });
}

void MarkInteractiveRendering(GLFWwindow* window, bool sizeChanged = false)
{
  auto* ctx =
    static_cast<ViewerInputContext*>(glfwGetWindowUserPointer(window));
  if (!ctx) return;

  ctx->lastInteraction = std::chrono::steady_clock::now();
  const bool enteringInteractive = !ctx->interactiveRendering;
  ctx->interactiveRendering = true;
  if (enteringInteractive || sizeChanged) {
    SendFramebufferSize(window);
    SendFrameRequest(window);
  }
}

void RestoreIdleRenderingIfDue(GLFWwindow* window,
                               std::chrono::steady_clock::time_point now)
{
  auto* ctx =
    static_cast<ViewerInputContext*>(glfwGetWindowUserPointer(window));
  if (!ctx || !ctx->interactiveRendering ||
      ctx->leftDown || ctx->rightDown || ctx->middleDown ||
      !ctx->pressedKeys.empty() ||
      now - ctx->lastInteraction <
        std::chrono::milliseconds(ctx->idleRestoreDelayMs)) {
    return;
  }

  ctx->interactiveRendering = false;
  SendFramebufferSize(window);
  SendFrameRequest(window);
}

void SendTextInput(GLFWwindow* window, const std::string& text)
{
  constexpr std::size_t MaxChunkBytes = 16 * 1024;
  std::size_t offset = 0;
  while (offset < text.size()) {
    std::size_t end = std::min(offset + MaxChunkBytes, text.size());
    while (end < text.size() && end > offset &&
           (static_cast<unsigned char>(text[end]) & 0xc0u) == 0x80u) {
      --end;
    }
    if (end == offset) {
      end = std::min(offset + MaxChunkBytes, text.size());
    }
    SendInput(window, {
      {"type", "text"},
      {"text", text.substr(offset, end - offset)}
    });
    offset = end;
  }
}

std::string KeyName(int key)
{
  if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
    return std::string(1, static_cast<char>('A' + key - GLFW_KEY_A));
  }
  if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
    return "Digit" + std::to_string(key - GLFW_KEY_0);
  }
  if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F24) {
    return "F" + std::to_string(key - GLFW_KEY_F1 + 1);
  }
  if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) {
    return "Keypad" + std::to_string(key - GLFW_KEY_KP_0);
  }
  switch (key) {
  case GLFW_KEY_ESCAPE: return "Escape";
  case GLFW_KEY_TAB: return "Tab";
  case GLFW_KEY_ENTER: return "Enter";
  case GLFW_KEY_BACKSPACE: return "Backspace";
  case GLFW_KEY_INSERT: return "Insert";
  case GLFW_KEY_DELETE: return "Delete";
  case GLFW_KEY_SPACE: return "Space";
  case GLFW_KEY_LEFT: return "Left";
  case GLFW_KEY_RIGHT: return "Right";
  case GLFW_KEY_UP: return "Up";
  case GLFW_KEY_DOWN: return "Down";
  case GLFW_KEY_PAGE_UP: return "PageUp";
  case GLFW_KEY_PAGE_DOWN: return "PageDown";
  case GLFW_KEY_HOME: return "Home";
  case GLFW_KEY_END: return "End";
  case GLFW_KEY_CAPS_LOCK: return "CapsLock";
  case GLFW_KEY_SCROLL_LOCK: return "ScrollLock";
  case GLFW_KEY_NUM_LOCK: return "NumLock";
  case GLFW_KEY_PRINT_SCREEN: return "PrintScreen";
  case GLFW_KEY_PAUSE: return "Pause";
  case GLFW_KEY_LEFT_SHIFT: return "LeftShift";
  case GLFW_KEY_RIGHT_SHIFT: return "RightShift";
  case GLFW_KEY_LEFT_CONTROL: return "LeftCtrl";
  case GLFW_KEY_RIGHT_CONTROL: return "RightCtrl";
  case GLFW_KEY_LEFT_ALT: return "LeftAlt";
  case GLFW_KEY_RIGHT_ALT: return "RightAlt";
  case GLFW_KEY_LEFT_SUPER: return "LeftSuper";
  case GLFW_KEY_RIGHT_SUPER: return "RightSuper";
  case GLFW_KEY_MENU: return "Menu";
  case GLFW_KEY_APOSTROPHE: return "Apostrophe";
  case GLFW_KEY_COMMA: return "Comma";
  case GLFW_KEY_MINUS: return "Minus";
  case GLFW_KEY_PERIOD: return "Period";
  case GLFW_KEY_SLASH: return "Slash";
  case GLFW_KEY_SEMICOLON: return "Semicolon";
  case GLFW_KEY_EQUAL: return "Equal";
  case GLFW_KEY_LEFT_BRACKET: return "LeftBracket";
  case GLFW_KEY_BACKSLASH: return "Backslash";
  case GLFW_KEY_RIGHT_BRACKET: return "RightBracket";
  case GLFW_KEY_GRAVE_ACCENT: return "GraveAccent";
  case GLFW_KEY_KP_DECIMAL: return "KeypadDecimal";
  case GLFW_KEY_KP_DIVIDE: return "KeypadDivide";
  case GLFW_KEY_KP_MULTIPLY: return "KeypadMultiply";
  case GLFW_KEY_KP_SUBTRACT: return "KeypadSubtract";
  case GLFW_KEY_KP_ADD: return "KeypadAdd";
  case GLFW_KEY_KP_ENTER: return "KeypadEnter";
  case GLFW_KEY_KP_EQUAL: return "KeypadEqual";
  default: return "";
  }
}

std::string ActionName(int action)
{
  if (action == GLFW_RELEASE) return "Release";
  if (action == GLFW_REPEAT) return "Repeat";
  return "Press";
}

void CursorCallback(GLFWwindow* window, double xpos, double ypos)
{
  MarkInteractiveRendering(window);
  auto* ctx =
    static_cast<ViewerInputContext*>(glfwGetWindowUserPointer(window));
  const bool leftDown = ctx ? ctx->leftDown : false;
  const PointerPosition position = MapPointerToRemote(window, xpos, ypos);
  SendInput(window, {
    {"type", "pointer_move"},
    {"x", position.x},
    {"y", position.y},
    {"primaryDown", leftDown},
    {"modifiers", BuildModifiers(window)},
    {"viewport", BuildViewport(window)}
  });
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int)
{
  MarkInteractiveRendering(window);
  auto* ctx =
    static_cast<ViewerInputContext*>(glfwGetWindowUserPointer(window));
  if (!ctx || (action != GLFW_PRESS && action != GLFW_RELEASE)) {
    return;
  }

  const char* buttonName = nullptr;
  bool* buttonDown = nullptr;
  if (button == GLFW_MOUSE_BUTTON_LEFT) {
    buttonName = "Left";
    buttonDown = &ctx->leftDown;
  } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
    buttonName = "Right";
    buttonDown = &ctx->rightDown;
  } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
    buttonName = "Middle";
    buttonDown = &ctx->middleDown;
  }
  if (!buttonName || !buttonDown) {
    return;
  }
  *buttonDown = action == GLFW_PRESS;

  double xpos = 0.0;
  double ypos = 0.0;
  glfwGetCursorPos(window, &xpos, &ypos);
  const PointerPosition position = MapPointerToRemote(window, xpos, ypos);
  SendInput(window, {
    {"type", "pointer_button"},
    {"button", buttonName},
    {"action", ActionName(action)},
    {"x", position.x},
    {"y", position.y},
    {"primaryDown", ctx->leftDown},
    {"modifiers", BuildModifiers(window)},
    {"viewport", BuildViewport(window)}
  });
}

void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
  MarkInteractiveRendering(window);
  double xpos = 0.0;
  double ypos = 0.0;
  glfwGetCursorPos(window, &xpos, &ypos);
  const PointerPosition position = MapPointerToRemote(window, xpos, ypos);
  SendInput(window, {
    {"type", "pointer_scroll"},
    {"x", position.x},
    {"y", position.y},
    {"wheelX", static_cast<float>(xoffset)},
    {"wheelY", static_cast<float>(yoffset)},
    {"modifiers", BuildModifiers(window)},
    {"viewport", BuildViewport(window)}
  });
}

void KeyCallback(GLFWwindow* window, int key, int, int action, int mods)
{
  const std::string keyName = KeyName(key);
  if (keyName.empty()) {
    return;
  }
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS &&
      glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE) {
    glfwRestoreWindow(window);
    return;
  }
  auto* ctx =
    static_cast<ViewerInputContext*>(glfwGetWindowUserPointer(window));
  if (ctx && action == GLFW_RELEASE &&
      ctx->locallyHandledKeys.erase(key) != 0) {
    return;
  }
  const bool pasteShortcut =
    key == GLFW_KEY_V && action == GLFW_PRESS &&
    (mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)) != 0;
  if (pasteShortcut) {
    const char* clipboard = glfwGetClipboardString(window);
    if (clipboard && clipboard[0] != '\0') {
      MarkInteractiveRendering(window);
      SendTextInput(window, clipboard);
    }
    if (ctx) ctx->locallyHandledKeys.insert(key);
    return;
  }
  MarkInteractiveRendering(window);
  if (ctx) {
    if (action == GLFW_PRESS) {
      ctx->pressedKeys.insert(key);
    } else if (action == GLFW_RELEASE) {
      ctx->pressedKeys.erase(key);
    }
  }

  SendInput(window, {
    {"type", "key"},
    {"key", keyName},
    {"action", ActionName(action)},
    {"modifiers", BuildModifiers(window)},
    {"viewport", BuildViewport(window)}
  });
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
  }
}

std::string EncodeUtf8(unsigned int codepoint)
{
  std::string text;
  if (codepoint <= 0x7f) {
    text.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7ff) {
    text.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
    text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0xffff &&
             !(codepoint >= 0xd800 && codepoint <= 0xdfff)) {
    text.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
    text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint >= 0x10000 && codepoint <= 0x10ffff) {
    text.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
    text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
    text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  }
  return text;
}

void CharCallback(GLFWwindow* window, unsigned int codepoint)
{
  const std::string text = EncodeUtf8(codepoint);
  if (!text.empty()) {
    MarkInteractiveRendering(window);
    SendTextInput(window, text);
  }
}

void FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
  if (width > 0 && height > 0) {
    MarkInteractiveRendering(window, true);
  }
}

void FocusCallback(GLFWwindow* window, int focused)
{
  if (focused) {
    return;
  }
  auto* ctx =
    static_cast<ViewerInputContext*>(glfwGetWindowUserPointer(window));
  if (!ctx) {
    return;
  }
  for (const int key : ctx->pressedKeys) {
    const std::string keyName = KeyName(key);
    if (!keyName.empty()) {
      SendInput(window, {
        {"type", "key"},
        {"key", keyName},
        {"action", "Release"},
        {"modifiers", BuildModifiers(window)}
      });
    }
  }
  ctx->pressedKeys.clear();
  ctx->locallyHandledKeys.clear();
  double xpos = 0.0;
  double ypos = 0.0;
  glfwGetCursorPos(window, &xpos, &ypos);
  const PointerPosition position = MapPointerToRemote(window, xpos, ypos);
  for (const auto button : {
         std::pair<const char*, bool*>{"Left", &ctx->leftDown},
         {"Right", &ctx->rightDown},
         {"Middle", &ctx->middleDown}}) {
    if (*button.second) {
      *button.second = false;
      SendInput(window, {
        {"type", "pointer_button"},
        {"button", button.first},
        {"action", "Release"},
        {"x", position.x},
        {"y", position.y},
        {"viewport", BuildViewport(window)}
      });
    }
  }
}

void PrintUsage(const char* argv0)
{
  std::cerr << "Usage: " << argv0
            << " [frame_endpoint] [input_endpoint] [still_endpoint]\n"
            << "Example: " << argv0
            << " tcp://127.0.0.1:5560 tcp://127.0.0.1:5561\n";
}

} // namespace

int main(int argc, char** argv)
{
  std::string endpoint = "tcp://127.0.0.1:5560";
  std::string inputEndpoint;
  std::string stillEndpoint = "tcp://127.0.0.1:5572";
  const bool boundedTransport = []() {
    const char* value = std::getenv("PARTICLE_VIS_REMOTE_TRANSPORT");
    return value && std::string(value) == "bounded";
  }();
  if (argc >= 2) {
    endpoint = argv[1];
  }
  if (argc >= 3) {
    inputEndpoint = argv[2];
  }
  if (argc >= 4) stillEndpoint = argv[3];
  if (argc > 4) {
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  zmq::context_t zmqContext{1};
  zmq::socket_t sub{zmqContext, boundedTransport
    ? zmq::socket_type::pull : zmq::socket_type::sub};
  if (!boundedTransport) sub.set(zmq::sockopt::subscribe, "");
  sub.set(zmq::sockopt::rcvhwm, 2);
  sub.connect(endpoint);
  std::unique_ptr<zmq::socket_t> stillSub;
  if (boundedTransport) {
    stillSub = std::make_unique<zmq::socket_t>(zmqContext, zmq::socket_type::pull);
    stillSub->set(zmq::sockopt::rcvhwm, 1);
    stillSub->connect(stillEndpoint);
  }

  zmq::socket_t inputPush{zmqContext, zmq::socket_type::push};
  bool inputEnabled = false;
  if (!inputEndpoint.empty()) {
    inputPush.set(zmq::sockopt::sndhwm, 256);
    inputPush.connect(inputEndpoint);
    inputEnabled = true;
  }

  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW\n";
    return EXIT_FAILURE;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  const int initialWidth = EnvInt("PARTICLE_VIS_VIEWER_WIDTH", 1280);
  const int initialHeight = EnvInt("PARTICLE_VIS_VIEWER_HEIGHT", 720);
  GLFWwindow* window = glfwCreateWindow(initialWidth,
                                        initialHeight,
                                        "Particle Vis Remote Viewer",
                                        nullptr,
                                        nullptr);
  if (!window) {
    std::cerr << "Failed to create GLFW window\n";
    glfwTerminate();
    return EXIT_FAILURE;
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  ViewerInputContext inputContext;
  inputContext.input = inputEnabled ? &inputPush : nullptr;
  inputContext.interactiveRenderScale =
    EnvFloat("PARTICLE_VIS_VIEWER_RENDER_SCALE", 0.75f);
  inputContext.idleRenderScale =
    EnvFloat("PARTICLE_VIS_VIEWER_IDLE_RENDER_SCALE", 2.0f);
  inputContext.idleRestoreDelayMs =
    EnvInt("PARTICLE_VIS_VIEWER_IDLE_DELAY_MS", 1500);
  const int logEveryNFrames =
    EnvInt("PARTICLE_VIS_VIEWER_LOG_EVERY_N_FRAMES", 60);
  glfwSetWindowUserPointer(window, &inputContext);
  glfwSetCursorPosCallback(window, CursorCallback);
  glfwSetMouseButtonCallback(window, MouseButtonCallback);
  glfwSetScrollCallback(window, ScrollCallback);
  glfwSetKeyCallback(window, KeyCallback);
  glfwSetCharCallback(window, CharCallback);
  glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
  glfwSetWindowFocusCallback(window, FocusCallback);
  SendFramebufferSize(window);
  SendFrameRequest(window);

  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
    std::cerr << "Failed to initialize GLAD\n";
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_FAILURE;
  }

  GLuint program = CreateProgram();
  if (!program) {
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_FAILURE;
  }

  const float quad[] = {
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
    -1.0f,  1.0f, 0.0f, 1.0f
  };
  const uint32_t indices[] = {0, 1, 2, 2, 3, 0};

  GLuint vao = 0;
  GLuint vbo = 0;
  GLuint ebo = 0;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ebo);

  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        4 * sizeof(float),
                        reinterpret_cast<void*>(2 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);

  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  std::cout << "Remote viewer connected to " << endpoint << '\n';
  if (boundedTransport)
    std::cout << "Remote still viewer connected to " << stillEndpoint << '\n';
  if (inputEnabled) {
    std::cout << "Remote input connected to " << inputEndpoint << '\n';
  }

  RemoteFrame frame;
  RemoteVideoDecoder videoDecoder;
  int textureWidth = 0;
  int textureHeight = 0;
  uint64_t displayedFrameCount = 0;
  uint64_t lastReceivedVideoFrameId = 0;
  uint64_t lastReceivedStillFrameId = 0;
  auto nextInitialFrameRequest = std::chrono::steady_clock::now();
  auto nextReceiptRepeat = std::chrono::steady_clock::now();
  auto nextTitleUpdate = std::chrono::steady_clock::now();

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    const auto now = std::chrono::steady_clock::now();
    RestoreIdleRenderingIfDue(window, now);
    if (textureWidth == 0 && now >= nextInitialFrameRequest) {
      SendFrameRequest(window);
      nextInitialFrameRequest = now + std::chrono::milliseconds(250);
    }
    if (boundedTransport && now >= nextReceiptRepeat) {
      if (lastReceivedVideoFrameId)
        SendFrameRequest(window, lastReceivedVideoFrameId);
      if (lastReceivedStillFrameId)
        SendFrameRequest(window, lastReceivedStillFrameId);
      nextReceiptRepeat = now + std::chrono::milliseconds(500);
    }

    RemoteFrame incoming;
    const RequestedPresentation requested =
      GetRequestedPresentation(window);
    while (ReceiveFrame(sub, incoming, requested.framebufferWidth,
                        requested.framebufferHeight, videoDecoder) ||
           (stillSub && ReceiveFrame(*stillSub, incoming,
                                     requested.framebufferWidth,
                                     requested.framebufferHeight,
                                     videoDecoder))) {
      if (!inputContext.initialResizeSent) {
        SendFramebufferSize(window);
        inputContext.initialResizeSent = true;
      }
      SendFrameRequest(window, incoming.frameId);
      if (incoming.idlePresentation)
        lastReceivedStillFrameId = incoming.frameId;
      else
        lastReceivedVideoFrameId = incoming.frameId;
      if (incoming.width != requested.framebufferWidth ||
          incoming.height != requested.framebufferHeight) {
        continue;
      }
      if (incoming.cameraGeneration < frame.cameraGeneration ||
          (incoming.cameraGeneration == frame.cameraGeneration &&
           incoming.frameId < frame.frameId) ||
          (boundedTransport && incoming.idlePresentation &&
           inputContext.interactiveRendering)) {
        continue;
      }
      RecordInputToReceive(inputContext, incoming);
      frame = std::move(incoming);
      inputContext.remoteWidth = frame.width;
      inputContext.remoteHeight = frame.height;
      inputContext.remoteDisplayWidth = frame.displayWidth;
      inputContext.remoteDisplayHeight = frame.displayHeight;
      inputContext.remoteFramebufferScaleX = frame.framebufferScaleX;
      inputContext.remoteFramebufferScaleY = frame.framebufferScaleY;
    }

    if (!frame.rgba.empty()) {
      const auto uploadStart = std::chrono::steady_clock::now();
      const bool textureSizeChanged =
        frame.width != textureWidth || frame.height != textureHeight;
      glBindTexture(GL_TEXTURE_2D, texture);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      if (textureSizeChanged) {
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA8,
                     frame.width,
                     frame.height,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     frame.rgba.data());
        textureWidth = frame.width;
        textureHeight = frame.height;
      } else {
        glTexSubImage2D(GL_TEXTURE_2D,
                        0,
                        0,
                        0,
                        frame.width,
                        frame.height,
                        GL_RGBA,
                        GL_UNSIGNED_BYTE,
                        frame.rgba.data());
      }
      const double uploadMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - uploadStart).count();
      ++displayedFrameCount;
      if (textureSizeChanged ||
          displayedFrameCount % static_cast<uint64_t>(logEveryNFrames) == 0) {
        std::cout << "Remote frame " << frame.frameId << ": "
                  << textureWidth << "x" << textureHeight << " ("
                  << frame.payloadBytes << " " << frame.encoding
                  << " bytes; "
                  << static_cast<std::size_t>(textureWidth) *
                       static_cast<std::size_t>(textureHeight) * 4
                  << " decoded RGBA bytes), display " << frame.displayWidth << "x"
                  << frame.displayHeight << ", scale "
                  << frame.framebufferScaleX << "x"
                  << frame.framebufferScaleY
                  << ", generation " << frame.cameraGeneration
                  << ", generation lag "
                  << (inputContext.nextInputSequence > frame.cameraGeneration
                        ? inputContext.nextInputSequence - frame.cameraGeneration : 0)
                  << ", outstanding " << frame.outstandingFrames
                  << ", app queue " << frame.applicationQueueDepth
                  << ", send backpressure " << frame.sendBackpressureCount
                  << ", displayed age ms "
                  << std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() -
                       (frame.inputSentAt.time_since_epoch().count()
                          ? frame.inputSentAt : frame.receivedAt)).count()
                  << "; timing ms: trigger to readback "
                  << frame.serverTriggerToReadbackMs
                  << ", input to frame start "
                  << frame.serverInputToFrameStartMs
                  << ", frame to render " << frame.serverFrameToRenderMs
                  << ", render " << frame.serverRenderMs
                  << ", readback latency "
                  << frame.serverReadbackLatencyMs << ", readback copy "
                  << frame.serverReadbackMs << ", encode queue "
                  << frame.serverEncoderQueueMs << ", encode "
                  << frame.serverEncodeMs << ", encode to send "
                  << frame.serverEncodeToSendMs << ", receive payload "
                  << frame.clientPayloadReceiveMs << ", previous send "
                  << frame.serverPreviousSendMs << ", decode "
                  << frame.clientDecodeMs << ", upload "
                  << uploadMs;
        if (frame.clientInputToReceiveMs >= 0.0) {
          const double measuredServerMs =
            frame.serverTriggerToReadbackMs +
            frame.serverReadbackLatencyMs +
            frame.serverEncoderQueueMs + frame.serverEncodeMs;
          const double transportResidualMs =
            std::max(0.0, frame.clientInputToReceiveMs - measuredServerMs);
          std::cout << ", input to receive " << frame.clientInputToReceiveMs
                    << ", transport/unmeasured " << transportResidualMs
                    << ", input to display "
                    << frame.clientInputToReceiveMs +
                         frame.clientDecodeMs + uploadMs;
        }
        if (logEveryNFrames == 1) {
          const auto clientReceiveNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
              frame.receivedAt.time_since_epoch()).count();
          const auto clientDisplayNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now().time_since_epoch()).count();
          std::cout << ", timeline ns server input/camera/render/encode/send "
                    << frame.serverInputReceivedAtNs << '/'
                    << frame.serverCameraUpdatedAtNs << '/'
                    << frame.serverRenderStartedAtNs << '/'
                    << frame.serverRenderFinishedAtNs << '/'
                    << frame.serverEncodeStartedAtNs << '/'
                    << frame.serverEncodeFinishedAtNs << '/'
                    << frame.serverSendAttemptAtNs
                    << ", client receive/display "
                    << clientReceiveNs << '/' << clientDisplayNs;
        }
        std::cout << ')' << std::endl;
      }
      frame.rgba.clear();
    }

    if (boundedTransport && now >= nextTitleUpdate && textureWidth > 0) {
      const auto origin = frame.inputSentAt.time_since_epoch().count()
        ? frame.inputSentAt : frame.receivedAt;
      const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - origin).count();
      const uint64_t lag = inputContext.nextInputSequence > frame.cameraGeneration
        ? inputContext.nextInputSequence - frame.cameraGeneration : 0;
      const std::string title = "Particle Vis Remote - generation lag " +
        std::to_string(lag) + ", frame age " + std::to_string(ageMs) + " ms";
      glfwSetWindowTitle(window, title.c_str());
      nextTitleUpdate = now + std::chrono::milliseconds(250);
    }

    int fbW = 0;
    int fbH = 0;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);
    glClearColor(0.03f, 0.03f, 0.035f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (textureWidth > 0 && textureHeight > 0) {
      glUseProgram(program);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texture);
      glUniform1i(glGetUniformLocation(program, "uFrame"), 0);
      glBindVertexArray(vao);
      glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
      glBindVertexArray(0);
    }

    glfwSwapBuffers(window);
  }

  glDeleteTextures(1, &texture);
  glDeleteBuffers(1, &ebo);
  glDeleteBuffers(1, &vbo);
  glDeleteVertexArrays(1, &vao);
  glDeleteProgram(program);

  glfwDestroyWindow(window);
  glfwTerminate();
  return EXIT_SUCCESS;
}
