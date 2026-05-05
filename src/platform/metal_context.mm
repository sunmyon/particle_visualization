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

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <utility>

struct MetalContext::Impl {
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> commandQueue = nil;
  CAMetalLayer* layer = nil;
  MTLRenderPassDescriptor* renderPass = nil;
  id<MTLTexture> depthTexture = nil;
  id<MTLTexture> headlessTexture = nil;
  id<CAMetalDrawable> drawable = nil;
  id<MTLCommandBuffer> commandBuffer = nil;
  id<MTLRenderCommandEncoder> encoder = nil;
  int depthWidth = 0;
  int depthHeight = 0;
  int headlessWidth = 0;
  int headlessHeight = 0;
  bool headless = false;
  bool initializedGlfwForHeadless = false;
};

namespace {

id<MTLDevice> CreateMetalDevice()
{
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  if (device) {
    return device;
  }

  NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
  if (devices.count > 0) {
    return devices[0];
  }
  return nil;
}

} // namespace

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

  impl_->device = CreateMetalDevice();
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
  impl_->layer.framebufferOnly = NO;

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
  if (width <= 0 || height <= 0) {
    return false;
  }

  [NSApplication sharedApplication];
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  impl_->initializedGlfwForHeadless = glfwInit() == GLFW_TRUE;
#endif
  impl_->device = CreateMetalDevice();
  if (!impl_->device) {
    std::cerr << "Metal: no system default MTLDevice." << std::endl;
    return false;
  }

  impl_->commandQueue = [impl_->device newCommandQueue];
  if (!impl_->commandQueue) {
    std::cerr << "Metal: failed to create command queue." << std::endl;
    return false;
  }

  impl_->renderPass = [MTLRenderPassDescriptor new];
  impl_->headless = true;
  impl_->headlessWidth = width;
  impl_->headlessHeight = height;
  std::cerr << "Metal headless device: "
            << [[impl_->device name] UTF8String] << std::endl;
  return true;
}

void MetalContext::destroy()
{
  if (!impl_) {
    return;
  }
  impl_->encoder = nil;
  impl_->commandBuffer = nil;
  impl_->drawable = nil;
  impl_->headlessTexture = nil;
  impl_->depthTexture = nil;
  impl_->renderPass = nil;
  impl_->layer = nil;
  impl_->commandQueue = nil;
  impl_->device = nil;
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (impl_->initializedGlfwForHeadless) {
    glfwTerminate();
  }
#endif
  impl_->headless = false;
  impl_->initializedGlfwForHeadless = false;
  impl_->headlessWidth = 0;
  impl_->headlessHeight = 0;
}

void MetalContext::present(NativeWindowHandle window)
{
  (void)window;
  if (!impl_ || !impl_->commandBuffer) {
    return;
  }
  if (impl_->encoder) {
    [impl_->encoder endEncoding];
    impl_->encoder = nil;
  }
  if (impl_->drawable) {
    [impl_->commandBuffer presentDrawable:impl_->drawable];
  }
  [impl_->commandBuffer commit];
  impl_->commandBuffer = nil;
  impl_->drawable = nil;
}

RenderedFrame MetalContext::readDefaultFramebuffer(int width, int height)
{
  RenderedFrame out;
  out.width = width;
  out.height = height;
  if (!impl_ || !impl_->commandBuffer || width <= 0 || height <= 0) {
    return out;
  }

  if (impl_->encoder) {
    [impl_->encoder endEncoding];
    impl_->encoder = nil;
  }

  id<MTLTexture> source = impl_->headless ? impl_->headlessTexture
                                          : impl_->drawable.texture;
  if (!source) {
    return out;
  }

  const NSUInteger srcWidth = source.width;
  const NSUInteger srcHeight = source.height;
  const int copyWidth = std::min<int>(width, static_cast<int>(srcWidth));
  const int copyHeight = std::min<int>(height, static_cast<int>(srcHeight));
  if (copyWidth <= 0 || copyHeight <= 0) {
    return out;
  }

  const std::size_t rawBytesPerRow =
    static_cast<std::size_t>(copyWidth) * 4u;
  const std::size_t bytesPerRow = ((rawBytesPerRow + 255u) / 256u) * 256u;
  const std::size_t stagingBytes =
    bytesPerRow * static_cast<std::size_t>(copyHeight);

  id<MTLBuffer> staging =
    [impl_->device newBufferWithLength:stagingBytes
                               options:MTLResourceStorageModeShared];
  if (!staging) {
    return out;
  }

  id<MTLBlitCommandEncoder> blit = [impl_->commandBuffer blitCommandEncoder];
  [blit copyFromTexture:source
            sourceSlice:0
            sourceLevel:0
           sourceOrigin:MTLOriginMake(0, 0, 0)
             sourceSize:MTLSizeMake(static_cast<NSUInteger>(copyWidth),
                                    static_cast<NSUInteger>(copyHeight),
                                    1)
               toBuffer:staging
      destinationOffset:0
 destinationBytesPerRow:bytesPerRow
destinationBytesPerImage:stagingBytes];
  [blit endEncoding];

  if (impl_->drawable) {
    [impl_->commandBuffer presentDrawable:impl_->drawable];
  }
  [impl_->commandBuffer commit];
  [impl_->commandBuffer waitUntilCompleted];

  out.format = RenderedFrameFormat::RGBA8;
  out.pixels.assign(static_cast<std::size_t>(width) *
                      static_cast<std::size_t>(height) * 4u,
                    0);

  const auto* src = static_cast<const std::uint8_t*>([staging contents]);
  for (int y = 0; y < copyHeight; ++y) {
    const std::uint8_t* row =
      src + static_cast<std::size_t>(y) * bytesPerRow;
    std::uint8_t* dst =
      out.pixels.data() +
      (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4u);
    for (int x = 0; x < copyWidth; ++x) {
      dst[x * 4u + 0u] = row[x * 4u + 2u];
      dst[x * 4u + 1u] = row[x * 4u + 1u];
      dst[x * 4u + 2u] = row[x * 4u + 0u];
      dst[x * 4u + 3u] = row[x * 4u + 3u];
    }
  }

  impl_->commandBuffer = nil;
  impl_->drawable = nil;
  return out;
}

bool MetalContext::isHeadless() const
{
  return impl_ && impl_->headless;
}

void* MetalContext::device() const
{
  return (__bridge void*)impl_->device;
}

void* MetalContext::currentCommandBuffer() const
{
  return (__bridge void*)impl_->commandBuffer;
}

void* MetalContext::currentRenderCommandEncoder() const
{
  return (__bridge void*)impl_->encoder;
}

void MetalContext::endCurrentRenderCommandEncoder()
{
  if (impl_->encoder) {
    if (impl_->renderPass && impl_->renderPass.depthAttachment.texture) {
      impl_->renderPass.depthAttachment.storeAction = MTLStoreActionStore;
    }
    [impl_->encoder endEncoding];
    impl_->encoder = nil;
  }
}

bool MetalContext::restartCurrentRenderCommandEncoder(bool loadColor,
                                                      bool loadDepth)
{
  if (!impl_->commandBuffer || !impl_->renderPass ||
      (!impl_->headless && (!impl_->drawable || !impl_->drawable.texture))) {
    return false;
  }
  if (impl_->encoder) {
    if (impl_->renderPass.depthAttachment.texture) {
      impl_->renderPass.depthAttachment.storeAction = MTLStoreActionStore;
    }
    [impl_->encoder endEncoding];
    impl_->encoder = nil;
  }

  auto* color = impl_->renderPass.colorAttachments[0];
  color.texture = impl_->headless ? impl_->headlessTexture
                                  : impl_->drawable.texture;
  if (!color.texture) {
    return false;
  }
  color.loadAction = loadColor ? MTLLoadActionLoad : MTLLoadActionClear;
  color.storeAction = MTLStoreActionStore;

  auto* depth = impl_->renderPass.depthAttachment;
  depth.texture = impl_->depthTexture;
  depth.loadAction = loadDepth ? MTLLoadActionLoad : MTLLoadActionClear;
  depth.storeAction = loadDepth ? MTLStoreActionStore : MTLStoreActionDontCare;

  impl_->encoder =
    [impl_->commandBuffer renderCommandEncoderWithDescriptor:impl_->renderPass];
  return impl_->encoder != nil;
}

bool MetalContext::beginFrame(int width, int height)
{
  if (!impl_->commandQueue || !impl_->renderPass ||
      width <= 0 || height <= 0) {
    return false;
  }

  if (impl_->headless) {
    if (!impl_->headlessTexture || impl_->headlessWidth != width ||
        impl_->headlessHeight != height) {
      MTLTextureDescriptor* colorDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:static_cast<NSUInteger>(width)
                                                          height:static_cast<NSUInteger>(height)
                                                       mipmapped:NO];
      colorDesc.usage = MTLTextureUsageRenderTarget |
                        MTLTextureUsageShaderRead |
                        MTLTextureUsageShaderWrite;
      colorDesc.storageMode = MTLStorageModePrivate;
      impl_->headlessTexture =
        [impl_->device newTextureWithDescriptor:colorDesc];
      impl_->headlessWidth = width;
      impl_->headlessHeight = height;
    }
  } else {
    if (!impl_->layer) {
      return false;
    }
    impl_->layer.drawableSize =
      CGSizeMake(static_cast<CGFloat>(width), static_cast<CGFloat>(height));
    impl_->drawable = [impl_->layer nextDrawable];
    if (!impl_->drawable) {
      return false;
    }
  }

  impl_->commandBuffer = [impl_->commandQueue commandBuffer];
  if (!impl_->commandBuffer) {
    impl_->drawable = nil;
    return false;
  }

  auto* color = impl_->renderPass.colorAttachments[0];
  color.texture = impl_->headless ? impl_->headlessTexture
                                  : impl_->drawable.texture;
  if (!color.texture) {
    impl_->commandBuffer = nil;
    impl_->drawable = nil;
    return false;
  }
  color.loadAction = MTLLoadActionClear;
  color.storeAction = MTLStoreActionStore;
  color.clearColor = MTLClearColorMake(0.02, 0.025, 0.03, 1.0);

  if (!impl_->depthTexture || impl_->depthWidth != width ||
      impl_->depthHeight != height) {
    MTLTextureDescriptor* depthDesc =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                         width:static_cast<NSUInteger>(width)
                                                        height:static_cast<NSUInteger>(height)
                                                     mipmapped:NO];
    depthDesc.usage = MTLTextureUsageRenderTarget;
    depthDesc.storageMode = MTLStorageModePrivate;
    impl_->depthTexture = [impl_->device newTextureWithDescriptor:depthDesc];
    impl_->depthWidth = width;
    impl_->depthHeight = height;
  }

  auto* depth = impl_->renderPass.depthAttachment;
  depth.texture = impl_->depthTexture;
  depth.loadAction = MTLLoadActionClear;
  depth.storeAction = MTLStoreActionDontCare;
  depth.clearDepth = 1.0;

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
  if (!impl_->encoder || !impl_->commandBuffer) {
    return;
  }
  if (preImGuiDraw_) {
    preImGuiDraw_();
  }
  ImGui_ImplMetal_RenderDrawData(drawData,
                                 impl_->commandBuffer,
                                 impl_->encoder);
  [impl_->encoder endEncoding];
  impl_->encoder = nil;
}

void MetalContext::setPreImGuiDrawCallback(PreImGuiDrawCallback callback)
{
  preImGuiDraw_ = std::move(callback);
}

std::unique_ptr<GraphicsContext> CreateMetalGraphicsContext()
{
  return std::make_unique<MetalContext>();
}
