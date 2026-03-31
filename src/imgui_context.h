#pragma once

struct GLFWwindow;

void InitImGuiContext(GLFWwindow* window);
void BeginImGuiFrame();
void EndImGuiFrame();
void ShutdownImGuiContext();
