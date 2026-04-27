#pragma once

struct RenderSystem;
struct AppState;
struct CallbackContext;
class OpenGLContext;
class WindowContext;

bool InitPlatform(WindowContext& window,
                  OpenGLContext& graphics,
                  CallbackContext& callbackCtx,
                  AppState& app);

void InitApplication(AppState& app, RenderSystem& render);
void LoadInitialData(AppState& app);
void Cleanup(AppState& app,
             RenderSystem& render,
             OpenGLContext& graphics,
             WindowContext& window);
