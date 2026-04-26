#pragma once

struct RenderSystem;
struct AppState;
struct CallbackContext;
class WindowContext;

bool InitPlatform(WindowContext& window,
                  CallbackContext& callbackCtx,
                  AppState& app);

void InitApplication(AppState& app, RenderSystem& render);
void LoadInitialData(AppState& app);
void Cleanup(AppState& app, RenderSystem& render, WindowContext& window);
