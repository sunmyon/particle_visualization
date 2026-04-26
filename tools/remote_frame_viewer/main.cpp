#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
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
  std::vector<uint8_t> rgba;
};

struct ViewerInputContext {
  zmq::socket_t* input = nullptr;
  bool leftDown = false;
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
      FragColor = texture(uFrame, vUv);
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
  glfwGetFramebufferSize(window, &width, &height);
  return {
    {"x", 0},
    {"y", 0},
    {"width", std::max(width, 1)},
    {"height", std::max(height, 1)},
    {"framebufferScaleX", 1.0f},
    {"framebufferScaleY", 1.0f}
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

std::string KeyName(int key)
{
  if (key == GLFW_KEY_ESCAPE) return "Escape";
  return "";
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
  SendInput(window, {
    {"type", "pointer_move"},
    {"x", static_cast<float>(xpos)},
    {"y", static_cast<float>(ypos)},
    {"primaryDown", leftDown},
    {"capturedByUI", false},
    {"modifiers", BuildModifiers(window)},
    {"viewport", BuildViewport(window)}
  });
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int)
{
  auto* ctx =
    static_cast<ViewerInputContext*>(glfwGetWindowUserPointer(window));
  if (ctx && button == GLFW_MOUSE_BUTTON_LEFT) {
    ctx->leftDown = (action == GLFW_PRESS);
  }

  double xpos = 0.0;
  double ypos = 0.0;
  glfwGetCursorPos(window, &xpos, &ypos);
  CursorCallback(window, xpos, ypos);
}

void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
  SendInput(window, {
    {"type", "pointer_scroll"},
    {"wheelX", static_cast<float>(xoffset)},
    {"wheelY", static_cast<float>(yoffset)},
    {"capturedByUI", false},
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

  SendInput(window, {
    {"type", "key"},
    {"key", keyName},
    {"action", ActionName(action)},
    {"capturedByUI", false},
    {"modifiers", BuildModifiers(window)},
    {"viewport", BuildViewport(window)}
  });
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
  sub.set(zmq::sockopt::conflate, 1);
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
      frame = std::move(incoming);
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
