#pragma once

class WindowContext;

void InitImGuiContext(WindowContext& window);
void InitHeadlessImGuiContext(int width, int height);
void BeginImGuiFrame(int width, int height);
void EndImGuiFrame();
void ShutdownImGuiContext();
