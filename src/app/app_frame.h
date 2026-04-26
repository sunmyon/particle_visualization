#pragma once

struct AppState;
struct RenderSystem;
class WindowContext;
class IFramePresenter;

void RunFrame(AppState& app,
              RenderSystem& render,
              WindowContext& window,
              IFramePresenter& presenter);
