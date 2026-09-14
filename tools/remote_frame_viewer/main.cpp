#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>
#include <zmq.hpp>

namespace {

struct RemoteFrame {
  uint64_t frameId = 0;
  int width = 0;
  int height = 0;
  int displayWidth = 0;
  int displayHeight = 0;
  float framebufferScaleX = 1.0f;
  float framebufferScaleY = 1.0f;
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
  bool initialResizeSent = false;
  std::unordered_set<int> pressedKeys;
};

struct PointerPosition {
  float x = 0.0f;
  float y = 0.0f;
};

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

bool ReceiveFrame(zmq::socket_t& sub, RemoteFrame& out)
{
  zmq::message_t headerMsg;
  auto headerResult = sub.recv(headerMsg, zmq::recv_flags::dontwait);
  if (!headerResult) {
    return false;
  }

  zmq::message_t payloadMsg;
  auto payloadResult = sub.recv(payloadMsg, zmq::recv_flags::none);
  if (!payloadResult) {
    return false;
  }

  nlohmann::json header =
    nlohmann::json::parse(headerMsg.to_string(), nullptr, false);
  if (header.is_discarded()) {
    return false;
  }

  if (header.value("type", "") != "rgba_frame") {
    return false;
  }

  const int width = header.value("width", 0);
  const int height = header.value("height", 0);
  const size_t expected =
    static_cast<size_t>(std::max(width, 0)) *
    static_cast<size_t>(std::max(height, 0)) * 4;
  if (width <= 0 || height <= 0 || payloadMsg.size() != expected) {
    return false;
  }

  out.frameId = header.value("frameId", uint64_t{0});
  out.width = width;
  out.height = height;
  out.displayWidth = header.value("displayWidth", width);
  out.displayHeight = header.value("displayHeight", height);
  out.framebufferScaleX = header.value("framebufferScaleX", 1.0f);
  out.framebufferScaleY = header.value("framebufferScaleY", 1.0f);
  out.rgba.resize(payloadMsg.size());
  std::copy(static_cast<const uint8_t*>(payloadMsg.data()),
            static_cast<const uint8_t*>(payloadMsg.data()) + payloadMsg.size(),
            out.rgba.begin());
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
  int width = 1;
  int height = 1;
  int framebufferWidth = 1;
  int framebufferHeight = 1;
  glfwGetWindowSize(window, &width, &height);
  glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
  return {
    {"x", 0},
    {"y", 0},
    {"width", std::max(width, 1)},
    {"height", std::max(height, 1)},
    {"framebufferScaleX", static_cast<float>(framebufferWidth) /
                            static_cast<float>(std::max(width, 1))},
    {"framebufferScaleY", static_cast<float>(framebufferHeight) /
                            static_cast<float>(std::max(height, 1))}
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

void SendInput(GLFWwindow* window, const nlohmann::json& event)
{
  auto* ctx =
    static_cast<ViewerInputContext*>(glfwGetWindowUserPointer(window));
  if (!ctx || !ctx->input) {
    return;
  }

  const std::string text = event.dump();
  try {
    ctx->input->send(zmq::buffer(text), zmq::send_flags::dontwait);
  } catch (const zmq::error_t&) {
  }
}

void SendFramebufferSize(GLFWwindow* window)
{
  int framebufferWidth = 0;
  int framebufferHeight = 0;
  int displayWidth = 0;
  int displayHeight = 0;
  glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
  glfwGetWindowSize(window, &displayWidth, &displayHeight);
  if (framebufferWidth <= 0 || framebufferHeight <= 0 ||
      displayWidth <= 0 || displayHeight <= 0) {
    return;
  }
  SendInput(window, {
    {"type", "framebuffer_resize"},
    {"width", framebufferWidth},
    {"height", framebufferHeight},
    {"displayWidth", displayWidth},
    {"displayHeight", displayHeight},
    {"framebufferScaleX", static_cast<float>(framebufferWidth) /
                            static_cast<float>(displayWidth)},
    {"framebufferScaleY", static_cast<float>(framebufferHeight) /
                            static_cast<float>(displayHeight)}
  });
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

void KeyCallback(GLFWwindow* window, int key, int, int action, int)
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
    SendInput(window, {{"type", "text"}, {"text", text}});
  }
}

void FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
  if (width > 0 && height > 0) {
    SendFramebufferSize(window);
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
            << " [frame_endpoint] [input_endpoint]\n"
            << "Example: " << argv0
            << " tcp://127.0.0.1:5560 tcp://127.0.0.1:5561\n";
}

} // namespace

int main(int argc, char** argv)
{
  std::string endpoint = "tcp://127.0.0.1:5560";
  std::string inputEndpoint;
  if (argc >= 2) {
    endpoint = argv[1];
  }
  if (argc >= 3) {
    inputEndpoint = argv[2];
  }
  if (argc > 3) {
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  zmq::context_t zmqContext{1};
  zmq::socket_t sub{zmqContext, zmq::socket_type::sub};
  sub.set(zmq::sockopt::subscribe, "");
  sub.set(zmq::sockopt::rcvhwm, 2);
  sub.connect(endpoint);

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

  GLFWwindow* window =
    glfwCreateWindow(1280, 720, "Particle Vis Remote Viewer", nullptr, nullptr);
  if (!window) {
    std::cerr << "Failed to create GLFW window\n";
    glfwTerminate();
    return EXIT_FAILURE;
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  ViewerInputContext inputContext;
  inputContext.input = inputEnabled ? &inputPush : nullptr;
  glfwSetWindowUserPointer(window, &inputContext);
  glfwSetCursorPosCallback(window, CursorCallback);
  glfwSetMouseButtonCallback(window, MouseButtonCallback);
  glfwSetScrollCallback(window, ScrollCallback);
  glfwSetKeyCallback(window, KeyCallback);
  glfwSetCharCallback(window, CharCallback);
  glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
  glfwSetWindowFocusCallback(window, FocusCallback);
  SendFramebufferSize(window);

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
  if (inputEnabled) {
    std::cout << "Remote input connected to " << inputEndpoint << '\n';
  }

  RemoteFrame frame;
  int textureWidth = 0;
  int textureHeight = 0;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    RemoteFrame incoming;
    while (ReceiveFrame(sub, incoming)) {
      if (!inputContext.initialResizeSent) {
        SendFramebufferSize(window);
        inputContext.initialResizeSent = true;
      }
      int desiredWidth = 0;
      int desiredHeight = 0;
      glfwGetFramebufferSize(window, &desiredWidth, &desiredHeight);
      if (incoming.width != desiredWidth || incoming.height != desiredHeight) {
        continue;
      }
      frame = std::move(incoming);
      inputContext.remoteWidth = frame.width;
      inputContext.remoteHeight = frame.height;
      inputContext.remoteDisplayWidth = frame.displayWidth;
      inputContext.remoteDisplayHeight = frame.displayHeight;
      inputContext.remoteFramebufferScaleX = frame.framebufferScaleX;
      inputContext.remoteFramebufferScaleY = frame.framebufferScaleY;
    }

    if (!frame.rgba.empty()) {
      glBindTexture(GL_TEXTURE_2D, texture);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      if (frame.width != textureWidth || frame.height != textureHeight) {
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
        std::cout << "Remote frame " << frame.frameId << ": "
                  << textureWidth << "x" << textureHeight << " ("
                  << static_cast<std::size_t>(textureWidth) *
                       static_cast<std::size_t>(textureHeight) * 4
                  << " RGBA bytes), display " << frame.displayWidth << "x"
                  << frame.displayHeight << ", scale "
                  << frame.framebufferScaleX << "x"
                  << frame.framebufferScaleY << std::endl;
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
      frame.rgba.clear();
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
