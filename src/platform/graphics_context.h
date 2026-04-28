#pragma once

#include <memory>

#include "render/rendered_frame.h"

struct NativeWindowHandle;

class GraphicsContext {
public:
  virtual ~GraphicsContext() = default;

  virtual void configureWindowHints() const = 0;
  virtual bool initFromWindow(NativeWindowHandle window) = 0;
  virtual bool initHeadless(int width, int height) = 0;
  virtual void destroy() = 0;
  virtual void present(NativeWindowHandle window) = 0;
  virtual RenderedFrame readDefaultFramebuffer(int width, int height) = 0;
  virtual bool isHeadless() const = 0;
};

std::unique_ptr<GraphicsContext> CreateDefaultGraphicsContext();
