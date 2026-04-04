#pragma once

#include <glad/glad.h>

unsigned int compileShaderWithHeader(unsigned int type,
                                     const char* header,
                                     const char* source);

unsigned int compileShader(unsigned int type,
                           const char* source);

unsigned int createShaderProgramWithHeader(const char* vertexSource,
                                           const char* fragmentSource,
                                           const char* header);

unsigned int createShaderProgram(const char* vertexSource,
                                 const char* fragmentSource);

extern const char* shaderHeader;
extern const char* shaderHeader410;
