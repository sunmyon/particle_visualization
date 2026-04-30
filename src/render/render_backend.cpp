#include "render/render_backend.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

std::string LowerCopy(std::string_view text)
{
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

} // namespace

RenderBackendKind ParseRenderBackendKind(std::string_view name,
                                         RenderBackendKind fallback)
{
  const std::string lower = LowerCopy(name);
  if (lower == "opengl" || lower == "gl") {
    return RenderBackendKind::OpenGL;
  }
  if (lower == "null" || lower == "none" || lower == "noop") {
    return RenderBackendKind::Null;
  }
  return fallback;
}

RenderBackendKind DefaultRenderBackendKind()
{
  constexpr RenderBackendKind fallback = RenderBackendKind::OpenGL;
  const char* env = std::getenv("PARTICLE_VIS_RENDER_BACKEND");
  if (!env || env[0] == '\0') {
    return fallback;
  }

  const RenderBackendKind kind = ParseRenderBackendKind(env, fallback);
  if (kind == fallback && LowerCopy(env) != "opengl" && LowerCopy(env) != "gl") {
    std::cerr << "Unknown PARTICLE_VIS_RENDER_BACKEND='" << env
              << "'; using OpenGL." << std::endl;
  }
  return kind;
}

std::unique_ptr<RenderBackend> CreateRenderBackend(RenderBackendKind kind)
{
  switch (kind) {
    case RenderBackendKind::OpenGL:
    #ifdef PARTICLE_VIS_ENABLE_OPENGL_BACKEND
      return CreateOpenGLRenderBackend();
    #else
      std::cerr << "OpenGL render backend is not linked; using null backend."
                << std::endl;
      return CreateNullRenderBackend();
    #endif
    case RenderBackendKind::Null:
      return CreateNullRenderBackend();
  }

  #ifdef PARTICLE_VIS_ENABLE_OPENGL_BACKEND
  return CreateOpenGLRenderBackend();
  #else
  return CreateNullRenderBackend();
  #endif
}
