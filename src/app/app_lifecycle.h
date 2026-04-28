#pragma once

struct RenderSystem;
struct AppState;

void InitApplication(AppState& app, RenderSystem& render);
void LoadInitialData(AppState& app);
void Cleanup(AppState& app, RenderSystem& render);
