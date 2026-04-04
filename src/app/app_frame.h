#pragma once

struct AppState;
struct RenderSystem;
class WindowContext;

void RunFrame(AppState& app,
              RenderSystem& render,
              WindowContext& window);
