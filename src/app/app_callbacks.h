#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

struct CallbackContext;

CallbackContext* GetCallbackContext(GLFWwindow* window);

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);
