#include "platform/metal_context.h"

#include "platform/window_backend.h"

#include <imgui_impl_metal.h>

#ifndef PARTICLE_VIS_HEADLESS_ONLY
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <iostream>
#include <utility>

struct MetalContext::Impl {
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> commandQueue = nil;
  CAMetalLayer* layer = nil;
  MTLRenderPassDescriptor* renderPass = nil;
  id<CAMetalDrawable> drawable = nil;
  id<MTLCommandBuffer> commandBuffer = nil;
  id<MTLRenderCommandEncoder> encoder = nil;
};

MetalContext::MetalContext()
  : impl_(std::make_unique<Impl>())
{
}

MetalContext::~MetalContext()
{
  destroy();
}

void MetalContext::configureWindowHints() const
{
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#endif
}

bool MetalContext::initFromWindow(NativeWindowHandle window)
{
#ifdef PARTICLE_VIS_HEADLESS_ONLY
  (void)window;
  return false;
#else
  if (window.backend != NativeWindowBackend::GLFW || !window.handle) {
    return false;
  }

  GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(window.handle);
  NSWindow* nsWindow = glfwGetCocoaWindow(glfwWindow);
  if (!nsWindow) {
    std::cerr << "Metal: failed to get Cocoa window from GLFW." << std::endl;
    return false;
  }

  impl_->device = MTLCreateSystemDefaultDevice();
  if (!impl_->device) {
    std::cerr << "Metal: no system default MTLDevice." << std::endl;
    return false;
  }

  impl_->commandQueue = [impl_->device newCommandQueue];
  if (!impl_->commandQueue) {
    std::cerr << "Metal: failed to create command queue." << std::endl;
    return false;
  }

  impl_->layer = [CAMetalLayer layer];
  impl_->layer.device = impl_->device;
  impl_->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  impl_->layer.framebufferOnly = YES;

  NSView* contentView = nsWindow.contentView;
  contentView.wantsLayer = YES;
  contentView.layer = impl_->layer;

  impl_->renderPass = [MTLRenderPassDescriptor new];
  std::cerr << "Metal device: "
            << [[impl_->device name] UTF8String] << std::endl;
  return true;
#endif
}

bool MetalContext::initHeadless(int width, int height)
{
  (void)width;
  (void)height;
  std::cerr << "Metal headless mode is not implemented yet." << std::endl;
  return false;
}

void MetalContext::destroy()
{
  if (!impl_) {
    return;
  }
  impl_->encoder = nil;
  impl_->commandBuffer = nil;
  impl_->drawable = nil;
  impl_->renderPass = nil;
  impl_->layer = nil;
  impl_->commandQueue = nil;
  impl_->device = nil;
}

void MetalContext::present(NativeWindowHandle window)
{
  (void)window;
}

RenderedFrame MetalContext::readDefaultFramebuffer(int width, int height)
{
  RenderedFrame out;
  out.width = width;
  out.height = height;
  return out;
}

void* MetalContext::device() const
{
  return (__bridge void*)impl_->device;
}

bool MetalContext::beginFrame(int width, int height)
{
  if (!impl_->layer || !impl_->commandQueue || !impl_->renderPass ||
      width <= 0 || height <= 0) {
    return false;
  }

  impl_->layer.drawableSize =
    CGSizeMake(static_cast<CGFloat>(width), static_cast<CGFloat>(height));
  impl_->drawable = [impl_->layer nextDrawable];
  if (!impl_->drawable) {
    return false;
  }

  impl_->commandBuffer = [impl_->commandQueue commandBuffer];
  if (!impl_->commandBuffer) {
    impl_->drawable = nil;
    return false;
  }

  auto* color = impl_->renderPass.colorAttachments[0];
  color.texture = impl_->drawable.texture;
  color.loadAction = MTLLoadActionClear;
  color.storeAction = MTLStoreActionStore;
  color.clearColor = MTLClearColorMake(0.02, 0.025, 0.03, 1.0);

  impl_->encoder =
    [impl_->commandBuffer renderCommandEncoderWithDescriptor:impl_->renderPass];
  return impl_->encoder != nil;
}

bool MetalContext::initImGuiRenderer()
{
  if (!impl_->device) {
    return false;
  }
  return ImGui_ImplMetal_Init(impl_->device);
}

void MetalContext::newImGuiFrame()
{
  ImGui_ImplMetal_NewFrame(impl_->renderPass);
}

void MetalContext::shutdownImGuiRenderer()
{
  ImGui_ImplMetal_Shutdown();
}

void MetalContext::renderImGuiDrawData(ImDrawData* drawData)
{
  if (!impl_->encoder || !impl_->commandBuffer || !impl_->drawable) {
    return;
  }
  if (preImGuiDraw_) {
    preImGuiDraw_();
  }
  ImGui_ImplMetal_RenderDrawData(drawData,
                                 impl_->commandBuffer,
                                 impl_->encoder);
  [impl_->encoder endEncoding];
  [impl_->commandBuffer presentDrawable:impl_->drawable];
  [impl_->commandBuffer commit];

  impl_->encoder = nil;
  impl_->commandBuffer = nil;
  impl_->drawable = nil;
}

void MetalContext::setPreImGuiDrawCallback(PreImGuiDrawCallback callback)
{
  preImGuiDraw_ = std::move(callback);
}

std::unique_ptr<GraphicsContext> CreateMetalGraphicsContext()
{
  return std::make_unique<MetalContext>();
}
