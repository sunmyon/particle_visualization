#pragma once

#include "app_state.h"
#include "render/render_system.h"
#include "window_context.h"

bool InitPlatform(WindowContext& window,
                  CallbackContext& callbackCtx,
                  AppState& app);

void InitApplication(AppState& app, RenderSystem& render);
void LoadInitialData(AppState& app);
void Cleanup(AppState& app, RenderSystem& render, WindowContext& window);
