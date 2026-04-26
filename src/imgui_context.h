#pragma once

struct GLFWwindow;

void InitImGuiContext(GLFWwindow* window);
void InitHeadlessImGuiContext(int width, int height);
void BeginImGuiFrame(int width, int height);
void EndImGuiFrame();
void ShutdownImGuiContext();
