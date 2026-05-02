#pragma once

#include "platform/graphics_context.h"

#include <functional>
#include <memory>

struct ImDrawData;

class MetalContext final : public GraphicsContext {
public:
  using PreImGuiDrawCallback = std::function<void()>;

  MetalContext();
  ~MetalContext() override;

  void configureWindowHints() const override;
  bool initFromWindow(NativeWindowHandle window) override;
  bool initHeadless(int width, int height) override;
  void destroy() override;
  void present(NativeWindowHandle window) override;
  RenderedFrame readDefaultFramebuffer(int width, int height) override;
  bool isHeadless() const override;

  void* device() const;
  void* currentCommandBuffer() const;
  void* currentRenderCommandEncoder() const;
  void endCurrentRenderCommandEncoder();
  bool restartCurrentRenderCommandEncoder(bool loadColor, bool loadDepth);
  bool beginFrame(int width, int height);
  bool initImGuiRenderer();
  void newImGuiFrame();
  void shutdownImGuiRenderer();
  void renderImGuiDrawData(ImDrawData* drawData);
  void setPreImGuiDrawCallback(PreImGuiDrawCallback callback);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  PreImGuiDrawCallback preImGuiDraw_;
};

std::unique_ptr<GraphicsContext> CreateMetalGraphicsContext();
