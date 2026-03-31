#include "shader_utils.h"

#include <iostream>
#include <cstdio>

const char* shaderHeader =
  "#version 330 core\n";

const char* shaderHeader410 =
  "#version 410 core\n";

unsigned int compileShaderWithHeader(unsigned int type,
                                     const char* header,
                                     const char* source)
{
  unsigned int shader = glCreateShader(type);
  const char* sources[] = { header, source };
  glShaderSource(shader, 2, sources, nullptr);
  glCompileShader(shader);

  GLint success = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char infoLog[4096];
    glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
    std::cerr << "ERROR::SHADER_COMPILATION_FAILED\n"
              << infoLog << std::endl;
    std::printf("ERROR::PROGRAM_LINKING_FAILED\n");
  }

  return shader;
}

unsigned int compileShader(unsigned int type,
                           const char* source)
{
  return compileShaderWithHeader(type, shaderHeader, source);
}

unsigned int createShaderProgramWithHeader(const char* vertexSource,
                                           const char* fragmentSource,
                                           const char* header)
{
  unsigned int vertexShader =
    compileShaderWithHeader(GL_VERTEX_SHADER, header, vertexSource);
  unsigned int fragmentShader =
    compileShaderWithHeader(GL_FRAGMENT_SHADER, header, fragmentSource);

  unsigned int program = glCreateProgram();
  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glLinkProgram(program);

  GLint success = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    char infoLog[511];
    glGetProgramInfoLog(program, 511, nullptr, infoLog);
    std::cerr << "ERROR::PROGRAM_LINKING_FAILED\n"
              << infoLog << std::endl;
    std::printf("ERROR::PROGRAM_LINKING_FAILED\n");
  }

  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  return program;
}

unsigned int createShaderProgram(const char* vertexSource,
                                 const char* fragmentSource)
{
  return createShaderProgramWithHeader(vertexSource, fragmentSource, shaderHeader);
}
