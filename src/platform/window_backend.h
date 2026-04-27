#pragma once

#include <memory>

class WindowBackend {
public:
  virtual ~WindowBackend() = default;

  virtual bool initLibrary() = 0;
  virtual bool createWindow(int width, int height, const char* title) = 0;
  virtual void destroy() = 0;

  virtual void* nativeHandle() const = 0;
  virtual void pollEvents() = 0;
  virtual void requestClose() = 0;
  virtual bool shouldClose(bool closeRequested) const = 0;
  virtual double timeSeconds() const = 0;
  virtual void framebufferSize(int& width, int& height) const = 0;
};

std::unique_ptr<WindowBackend> CreateDefaultWindowBackend();
