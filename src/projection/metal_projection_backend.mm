#include "projection/metal_projection_backend.h"

#import <Metal/Metal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr const char* kProjectionComputeSource = R"(
#include <metal_stdlib>
using namespace metal;

struct ProjectionParticle {
  packed_float3 pos;
  float val;
  float colorVal;
  float density;
  float mass;
  float hsml;
};

struct ProjectionUniforms {
  uint width;
  uint height;
  uint densityWeight;
  uint particleCount;
  float dx;
  float dy;
  float xminX;
  float xminY;
  float4 center;
  float4 uAxis;
  float4 vAxis;
  float valueMin;
  float valueMax;
  float pad0;
  float pad1;
};

struct VoronoiUniforms {
  uint width;
  uint height;
  uint depth;
  uint particleCount;
  float dx;
  float dy;
  float dz;
  float xminX;
  float xminY;
  float xminZ;
  uint densityWeight;
  uint pad0;
  float4 renderColor;
  uint tfCount;
  uint colorMapSize;
  uint colorLogScale;
  uint pad1;
  float colorValueMin;
  float colorValueMax;
  float2 pad2;
};

struct ProjectionTfComponent {
  uint type;
  float center;
  float width;
  float amplitude;
  uint logDomain;
  float3 pad;
};

float cubicKernel(float u)
{
  if (u < 0.5) {
    return 1.0 - 6.0 * u * u + 6.0 * u * u * u;
  }
  if (u < 1.0) {
    float oneMinusU = 1.0 - u;
    return 2.0 * oneMinusU * oneMinusU * oneMinusU;
  }
  return 0.0;
}

kernel void projectionSplatCompute(
  uint gid [[thread_position_in_grid]],
  constant ProjectionParticle* particles [[buffer(0)]],
  constant ProjectionUniforms& uniforms [[buffer(1)]],
  device atomic_float* accumValue [[buffer(2)]],
  device atomic_float* accumWeight [[buffer(3)]])
{
  if (gid >= uniforms.particleCount) {
    return;
  }

  ProjectionParticle p = particles[gid];
  if (p.hsml <= 0.0 || !isfinite(p.hsml) || !isfinite(p.mass) ||
      !isfinite(p.density) || !isfinite(p.val)) {
    return;
  }

  float3 diff = float3(p.pos) - uniforms.center.xyz;
  float cx = dot(diff, uniforms.uAxis.xyz);
  float cy = dot(diff, uniforms.vAxis.xyz);
  float hsml = p.hsml;
  float hsml2 = hsml * hsml;

  int jMin = max(0, int(floor((cy - hsml - uniforms.xminY) / uniforms.dy)));
  int jMax = min(int(uniforms.height) - 1,
                 int(ceil((cy + hsml - uniforms.xminY) / uniforms.dy)) - 1);
  int iMin = max(0, int(floor((cx - hsml - uniforms.xminX) / uniforms.dx)));
  int iMax = min(int(uniforms.width) - 1,
                 int(ceil((cx + hsml - uniforms.xminX) / uniforms.dx)) - 1);
  if (iMin > iMax || jMin > jMax) {
    return;
  }

  float density = max(p.density, 1.0e-30);
  float weightNorm = p.mass / max(hsml * hsml2 * density, 1.0e-30);
  if (uniforms.densityWeight != 0u) {
    weightNorm *= p.density;
  }

  for (int j = jMin; j <= jMax; ++j) {
    float cellY = uniforms.xminY + (float(j) + 0.5) * uniforms.dy;
    float dy = cellY - cy;
    float dy2 = dy * dy;
    if (dy2 > hsml2) {
      continue;
    }

    for (int i = iMin; i <= iMax; ++i) {
      float cellX = uniforms.xminX + (float(i) + 0.5) * uniforms.dx;
      float dx = cellX - cx;
      float r2 = dx * dx + dy2;
      if (r2 > hsml2) {
        continue;
      }

      float kernelRadius = sqrt(r2) / hsml;
      float weight = cubicKernel(kernelRadius) * weightNorm;
      if (!(weight > 0.0) || !isfinite(weight)) {
        continue;
      }

      uint pixelIndex = uint(j) * uniforms.width + uint(i);
      if (weight > 0.0) {
        atomic_fetch_add_explicit(&accumWeight[pixelIndex],
                                  weight,
                                  memory_order_relaxed);
        atomic_fetch_add_explicit(&accumValue[pixelIndex],
                                  p.val * weight,
                                  memory_order_relaxed);
      }
    }
  }
}

float3 voxelCenter(uint index, constant VoronoiUniforms& uniforms)
{
  uint widthHeight = uniforms.width * uniforms.height;
  uint k = index / widthHeight;
  uint rem = index - k * widthHeight;
  uint j = rem / uniforms.width;
  uint i = rem - j * uniforms.width;

  return float3(uniforms.xminX + (float(i) + 0.5) * uniforms.dx,
                uniforms.xminY + (float(j) + 0.5) * uniforms.dy,
                uniforms.xminZ + float(k) * uniforms.dz);
}

kernel void voronoiJumpFlood(
  uint gid [[thread_position_in_grid]],
  constant ProjectionParticle* particles [[buffer(0)]],
  constant VoronoiUniforms& uniforms [[buffer(1)]],
  constant int* inputLabels [[buffer(2)]],
  device int* outputLabels [[buffer(3)]],
  constant uint& stepSize [[buffer(4)]])
{
  uint voxelCount = uniforms.width * uniforms.height * uniforms.depth;
  if (gid >= voxelCount) {
    return;
  }

  uint widthHeight = uniforms.width * uniforms.height;
  uint k = gid / widthHeight;
  uint rem = gid - k * widthHeight;
  uint j = rem / uniforms.width;
  uint i = rem - j * uniforms.width;
  float3 center = voxelCenter(gid, uniforms);

  int bestLabel = inputLabels[gid];
  float bestDist2 = INFINITY;
  if (bestLabel >= 0) {
    float3 pos = float3(particles[bestLabel].pos);
    float3 d = pos - center;
    bestDist2 = dot(d, d);
  }

  int s = int(stepSize);
  for (int dz = -s; dz <= s; dz += s) {
    int nk = int(k) + dz;
    if (nk < 0 || nk >= int(uniforms.depth)) {
      continue;
    }
    for (int dy = -s; dy <= s; dy += s) {
      int nj = int(j) + dy;
      if (nj < 0 || nj >= int(uniforms.height)) {
        continue;
      }
      for (int dx = -s; dx <= s; dx += s) {
        int ni = int(i) + dx;
        if (ni < 0 || ni >= int(uniforms.width)) {
          continue;
        }
        uint neighborIndex =
          uint(nk) * widthHeight + uint(nj) * uniforms.width + uint(ni);
        int label = inputLabels[neighborIndex];
        if (label < 0) {
          continue;
        }
        float3 pos = float3(particles[label].pos);
        float3 d = pos - center;
        float dist2 = dot(d, d);
        if (dist2 < bestDist2) {
          bestDist2 = dist2;
          bestLabel = label;
        }
      }
    }
  }

  outputLabels[gid] = bestLabel;
}

kernel void voronoiIntegrate(
  uint gid [[thread_position_in_grid]],
  constant ProjectionParticle* particles [[buffer(0)]],
  constant VoronoiUniforms& uniforms [[buffer(1)]],
  constant int* labels [[buffer(2)]],
  device float* outputValue [[buffer(3)]],
  device float* outputWeight [[buffer(4)]])
{
  uint pixelCount = uniforms.width * uniforms.height;
  if (gid >= pixelCount) {
    return;
  }

  float sumValue = 0.0;
  float sumWeight = 0.0;
  for (uint k = 0; k < uniforms.depth; ++k) {
    uint voxelIndex = k * pixelCount + gid;
    int label = labels[voxelIndex];
    if (label < 0) {
      continue;
    }

    ProjectionParticle p = particles[label];
    float hsml = max(p.hsml, 1.0e-30);
    float weight = p.mass / max(hsml * hsml * hsml, 1.0e-30);
    if (uniforms.densityWeight != 0u) {
      weight *= p.density;
    }
    if (weight > 0.0 && isfinite(weight) && isfinite(p.val)) {
      sumValue += p.val * weight;
      sumWeight += weight;
    }
  }

  outputValue[gid] = sumValue;
  outputWeight[gid] = sumWeight;
}

float evalProjectionTfComponent(constant ProjectionTfComponent& c, float value)
{
  if (!isfinite(value)) {
    return 0.0;
  }
  float width = max(abs(c.width), 1.0e-12);
  float amp = max(c.amplitude, 0.0);
  if (amp <= 0.0) {
    return 0.0;
  }

  if (c.type == 0u && c.logDomain != 0u) {
    if (value <= 0.0 || c.center <= 0.0) {
      return 0.0;
    }
    float x = log10(value);
    float center = log10(max(c.center, 1.0e-30));
    float q = (x - center) / width;
    return amp * exp(-0.5 * q * q);
  }

  float q = abs(value - c.center);
  if (c.type == 0u) {
    float u = q / width;
    return amp * exp(-0.5 * u * u);
  }
  if (c.type == 1u) {
    return q <= width ? amp : 0.0;
  }
  if (c.type == 2u) {
    return q <= width ? amp * (1.0 - q / width) : 0.0;
  }
  return 0.0;
}

float evalProjectionTf(constant VoronoiUniforms& uniforms,
                       constant ProjectionTfComponent* components,
                       float value)
{
  float sigma = 0.0;
  uint count = min(uniforms.tfCount, 16u);
  for (uint i = 0; i < count; ++i) {
    sigma += evalProjectionTfComponent(components[i], value);
  }
  return max(sigma, 0.0);
}

kernel void voronoiRender(
  uint gid [[thread_position_in_grid]],
  constant ProjectionParticle* particles [[buffer(0)]],
  constant VoronoiUniforms& uniforms [[buffer(1)]],
  constant int* labels [[buffer(2)]],
  device float* outputRgb [[buffer(3)]],
  device float* outputAlpha [[buffer(4)]],
  constant ProjectionTfComponent* tfComponents [[buffer(5)]],
  constant float* colorMap [[buffer(6)]])
{
  uint pixelCount = uniforms.width * uniforms.height;
  if (gid >= pixelCount) {
    return;
  }

  float3 accum = float3(0.0);
  float alphaAccum = 0.0;
  float dzLen = max(abs(uniforms.dz), 1.0e-30);
  uint colorMapSize = max(uniforms.colorMapSize, 1u);

  for (uint k = 0; k < uniforms.depth && alphaAccum < 0.999; ++k) {
    uint voxelIndex = k * pixelCount + gid;
    int label = labels[voxelIndex];
    if (label < 0) {
      continue;
    }

    ProjectionParticle p = particles[label];
    float sigma = evalProjectionTf(uniforms, tfComponents, p.val);
    if (!(sigma > 0.0) || !isfinite(sigma)) {
      continue;
    }
    float alpha = clamp(1.0 - exp(-sigma * dzLen), 0.0, 1.0);
    float colorValue = p.colorVal;
    if (uniforms.colorLogScale != 0u) {
      if (colorValue <= 0.0) {
        continue;
      }
      colorValue = log10(colorValue);
    }
    float t = clamp((colorValue - uniforms.colorValueMin) /
                    max(uniforms.colorValueMax - uniforms.colorValueMin,
                        1.0e-12),
                    0.0,
                    1.0);
    float x = t * float(colorMapSize - 1u);
    uint i0 = min(uint(floor(x)), colorMapSize - 1u);
    uint i1 = min(i0 + 1u, colorMapSize - 1u);
    float f = x - float(i0);
    float3 c0 = float3(colorMap[i0 * 3u + 0u],
                       colorMap[i0 * 3u + 1u],
                       colorMap[i0 * 3u + 2u]);
    float3 c1 = float3(colorMap[i1 * 3u + 0u],
                       colorMap[i1 * 3u + 1u],
                       colorMap[i1 * 3u + 2u]);
    float3 color = mix(c0, c1, f);
    float trans = 1.0 - alphaAccum;
    accum += trans * alpha * color;
    alphaAccum += trans * alpha;
  }

  outputRgb[gid * 3u + 0u] = accum.x;
  outputRgb[gid * 3u + 1u] = accum.y;
  outputRgb[gid * 3u + 2u] = accum.z;
  outputAlpha[gid] = alphaAccum;
}
)";

struct ProjectionUniformsCpu {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t densityWeight = 0;
  std::uint32_t particleCount = 0;
  float dx = 0.0f;
  float dy = 0.0f;
  float xminX = 0.0f;
  float xminY = 0.0f;
  float center[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float uAxis[4] = {1.0f, 0.0f, 0.0f, 0.0f};
  float vAxis[4] = {0.0f, 1.0f, 0.0f, 0.0f};
  float valueMin = 0.0f;
  float valueMax = 1.0f;
  float pad0 = 0.0f;
  float pad1 = 0.0f;
};

struct VoronoiUniformsCpu {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t depth = 0;
  std::uint32_t particleCount = 0;
  float dx = 0.0f;
  float dy = 0.0f;
  float dz = 0.0f;
  float xminX = 0.0f;
  float xminY = 0.0f;
  float xminZ = 0.0f;
  std::uint32_t densityWeight = 0;
  std::uint32_t pad0 = 0;
  float renderColor[4] = {0.35f, 1.0f, 0.8f, 0.0f};
  std::uint32_t tfCount = 0;
  std::uint32_t colorMapSize = 0;
  std::uint32_t colorLogScale = 0;
  std::uint32_t pad1 = 0;
  float colorValueMin = 0.0f;
  float colorValueMax = 1.0f;
  float pad2[2] = {0.0f, 0.0f};
};

struct ProjectionTfComponentCpu {
  std::uint32_t type = 0;
  float center = 1.0f;
  float width = 1.0f;
  float amplitude = 0.0f;
  std::uint32_t logDomain = 1;
  float pad[3] = {0.0f, 0.0f, 0.0f};
};

id<MTLDevice> CreateProjectionDevice()
{
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  if (device) {
    return device;
  }
  NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
  return devices.count > 0 ? devices[0] : nil;
}

bool CompilePipeline(id<MTLDevice> device,
                     __strong id<MTLComputePipelineState>& pipeline)
{
  if (pipeline) {
    return true;
  }

  NSError* error = nil;
  NSString* source = [NSString stringWithUTF8String:kProjectionComputeSource];
  id<MTLLibrary> library = [device newLibraryWithSource:source
                                                options:nil
                                                  error:&error];
  if (!library) {
    std::cerr << "Metal projection compute shader compilation failed: "
              << (error ? [[error localizedDescription] UTF8String]
                        : "unknown error")
              << std::endl;
    return false;
  }

  id<MTLFunction> kernel =
    [library newFunctionWithName:@"projectionSplatCompute"];
  if (!kernel) {
    std::cerr << "Metal projection compute shader entry point missing."
              << std::endl;
    return false;
  }

  pipeline = [device newComputePipelineStateWithFunction:kernel error:&error];
  if (!pipeline) {
    std::cerr << "Metal projection compute pipeline creation failed: "
              << (error ? [[error localizedDescription] UTF8String]
                        : "unknown error")
              << std::endl;
    return false;
  }
  return true;
}

bool CompileNamedPipeline(id<MTLDevice> device,
                          NSString* name,
                          __strong id<MTLComputePipelineState>& pipeline)
{
  if (pipeline) {
    return true;
  }

  NSError* error = nil;
  NSString* source = [NSString stringWithUTF8String:kProjectionComputeSource];
  id<MTLLibrary> library = [device newLibraryWithSource:source
                                                options:nil
                                                  error:&error];
  if (!library) {
    std::cerr << "Metal projection shader compilation failed: "
              << (error ? [[error localizedDescription] UTF8String]
                        : "unknown error")
              << std::endl;
    return false;
  }

  id<MTLFunction> kernel = [library newFunctionWithName:name];
  if (!kernel) {
    std::cerr << "Metal projection shader entry point missing: "
              << [name UTF8String] << std::endl;
    return false;
  }

  pipeline = [device newComputePipelineStateWithFunction:kernel error:&error];
  if (!pipeline) {
    std::cerr << "Metal projection pipeline creation failed for "
              << [name UTF8String] << ": "
              << (error ? [[error localizedDescription] UTF8String]
                        : "unknown error")
              << std::endl;
    return false;
  }
  return true;
}

} // namespace

bool RunMetalProjectionMap(const MetalProjectionMapInput& input,
                           MetalProjectionMapOutput& output)
{
  output = MetalProjectionMapOutput{};
  if (input.width <= 0 || input.height <= 0 || input.dx <= 0.0f ||
      input.dy <= 0.0f || input.particles.empty()) {
    return false;
  }

  static id<MTLDevice> device = CreateProjectionDevice();
  static id<MTLCommandQueue> queue = device ? [device newCommandQueue] : nil;
  static id<MTLComputePipelineState> pipeline = nil;
  if (!device || !queue || !CompilePipeline(device, pipeline)) {
    std::cerr << "Metal projection setup failed: device="
              << (device ? "yes" : "no")
              << " queue=" << (queue ? "yes" : "no")
              << " pipeline=" << (pipeline ? "yes" : "no")
              << std::endl;
    return false;
  }

  const std::size_t pixelCount =
    static_cast<std::size_t>(input.width) *
    static_cast<std::size_t>(input.height);
  const std::size_t accumBytes = pixelCount * sizeof(float);
  const std::size_t particleBytes =
    input.particles.size() * sizeof(MetalProjectionParticle);

  id<MTLBuffer> particleBuffer =
    [device newBufferWithBytes:input.particles.data()
                        length:particleBytes
                       options:MTLResourceStorageModeShared];
  id<MTLBuffer> valueBuffer =
    [device newBufferWithLength:accumBytes options:MTLResourceStorageModeShared];
  id<MTLBuffer> weightBuffer =
    [device newBufferWithLength:accumBytes options:MTLResourceStorageModeShared];
  if (!particleBuffer || !valueBuffer || !weightBuffer) {
    std::cerr << "Metal projection resource allocation failed: particleBuffer="
              << (particleBuffer ? "yes" : "no")
              << " valueBuffer=" << (valueBuffer ? "yes" : "no")
              << " weightBuffer=" << (weightBuffer ? "yes" : "no")
              << " accumBytes=" << accumBytes
              << " particleBytes=" << particleBytes
              << std::endl;
    return false;
  }

  std::fill_n(static_cast<float*>([valueBuffer contents]),
              pixelCount,
              0.0f);
  std::fill_n(static_cast<float*>([weightBuffer contents]),
              pixelCount,
              0.0f);

  ProjectionUniformsCpu uniforms;
  uniforms.width = static_cast<std::uint32_t>(input.width);
  uniforms.height = static_cast<std::uint32_t>(input.height);
  uniforms.densityWeight = input.densityWeight ? 1u : 0u;
  uniforms.particleCount = static_cast<std::uint32_t>(input.particles.size());
  uniforms.dx = input.dx;
  uniforms.dy = input.dy;
  uniforms.xminX = input.xminLocal[0];
  uniforms.xminY = input.xminLocal[1];
  uniforms.center[0] = input.center.x;
  uniforms.center[1] = input.center.y;
  uniforms.center[2] = input.center.z;
  uniforms.uAxis[0] = input.uAxis.x;
  uniforms.uAxis[1] = input.uAxis.y;
  uniforms.uAxis[2] = input.uAxis.z;
  uniforms.vAxis[0] = input.vAxis.x;
  uniforms.vAxis[1] = input.vAxis.y;
  uniforms.vAxis[2] = input.vAxis.z;
  uniforms.valueMin = input.valueMin;
  uniforms.valueMax = input.valueMax;

  id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
  id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
  if (!commandBuffer || !encoder) {
    std::cerr << "Metal projection compute encoder creation failed."
              << std::endl;
    return false;
  }

  const auto start = std::chrono::steady_clock::now();
  [encoder setComputePipelineState:pipeline];
  [encoder setBuffer:particleBuffer offset:0 atIndex:0];
  [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:1];
  [encoder setBuffer:valueBuffer offset:0 atIndex:2];
  [encoder setBuffer:weightBuffer offset:0 atIndex:3];

  const NSUInteger threadsPerGroup =
    std::min<NSUInteger>(pipeline.maxTotalThreadsPerThreadgroup,
                         std::max<NSUInteger>(pipeline.threadExecutionWidth,
                                             64u));
  const MTLSize gridSize =
    MTLSizeMake(static_cast<NSUInteger>(input.particles.size()), 1, 1);
  const MTLSize groupSize = MTLSizeMake(threadsPerGroup, 1, 1);
  [encoder dispatchThreads:gridSize threadsPerThreadgroup:groupSize];
  [encoder endEncoding];
  [commandBuffer commit];
  [commandBuffer waitUntilCompleted];
  const auto end = std::chrono::steady_clock::now();

  if (commandBuffer.status == MTLCommandBufferStatusError) {
    std::cerr << "Metal projection compute command failed: "
              << (commandBuffer.error
                    ? [[commandBuffer.error localizedDescription] UTF8String]
                    : "unknown error")
              << std::endl;
    return false;
  }

  output.values.resize(pixelCount);
  output.weights.resize(pixelCount);
  const auto* sumValues = static_cast<const float*>([valueBuffer contents]);
  const auto* sumWeights = static_cast<const float*>([weightBuffer contents]);

  std::size_t nonzeroWeights = 0;
  double weightSum = 0.0;
  float weightMax = 0.0f;
  float valueMin = std::numeric_limits<float>::max();
  float valueMax = -std::numeric_limits<float>::max();
  for (std::size_t i = 0; i < pixelCount; ++i) {
    output.values[i] = sumValues[i];
    output.weights[i] = sumWeights[i];
    if (output.weights[i] > 0.0f) {
      ++nonzeroWeights;
      weightSum += output.weights[i];
      weightMax = std::max(weightMax, output.weights[i]);
      valueMin = std::min(valueMin, output.values[i]);
      valueMax = std::max(valueMax, output.values[i]);
    }
  }

  output.elapsedMs =
    std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "Metal projection compute readback: particles="
            << input.particles.size()
            << " pixels=" << input.width << "x" << input.height
            << " nonzeroWeights=" << nonzeroWeights
            << " weightSum=" << weightSum
            << " weightMax=" << weightMax;
  if (nonzeroWeights > 0) {
    std::cout << " valueAccumRange=[" << valueMin << ", " << valueMax << "]";
  }
  std::cout << std::endl;
  if (nonzeroWeights == 0) {
    std::cerr << "Metal projection compute produced an empty weight buffer."
              << std::endl;
    return false;
  }
  return true;
}

bool BuildMetalVoronoiLabelGrid(const MetalProjectionMapInput& input,
                                MetalVoronoiLabelGrid& grid)
{
  grid = MetalVoronoiLabelGrid{};
  if (input.width <= 0 || input.height <= 0 || input.depth <= 0 ||
      input.dx <= 0.0f || input.dy <= 0.0f || input.dz <= 0.0f ||
      input.particles.empty()) {
    return false;
  }

  static id<MTLDevice> device = CreateProjectionDevice();
  static id<MTLCommandQueue> queue = device ? [device newCommandQueue] : nil;
  static id<MTLComputePipelineState> jfaPipeline = nil;
  if (!device || !queue ||
      !CompileNamedPipeline(device, @"voronoiJumpFlood", jfaPipeline)) {
    std::cerr << "Metal Voronoi projection setup failed: device="
              << (device ? "yes" : "no")
              << " queue=" << (queue ? "yes" : "no")
              << " jfaPipeline=" << (jfaPipeline ? "yes" : "no")
              << std::endl;
    return false;
  }

  const std::size_t pixelCount =
    static_cast<std::size_t>(input.width) *
    static_cast<std::size_t>(input.height);
  const std::size_t voxelCount = pixelCount * static_cast<std::size_t>(input.depth);
  const std::size_t particleBytes =
    input.particles.size() * sizeof(MetalProjectionParticle);
  const std::size_t labelBytes = voxelCount * sizeof(std::int32_t);

  std::vector<std::int32_t> seedLabels(voxelCount, -1);
  std::vector<float> seedDist2(voxelCount, std::numeric_limits<float>::infinity());
  const auto voxelIndex = [width = input.width, height = input.height](int i,
                                                                       int j,
                                                                       int k) {
    return static_cast<std::size_t>(k) *
             static_cast<std::size_t>(width) *
             static_cast<std::size_t>(height) +
           static_cast<std::size_t>(j) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(i);
  };

  for (std::size_t particleIndex = 0; particleIndex < input.particles.size();
       ++particleIndex) {
    const MetalProjectionParticle& p = input.particles[particleIndex];
    const int i = static_cast<int>(
      std::lround((p.pos[0] - (input.xminLocal[0] + 0.5f * input.dx)) / input.dx));
    const int j = static_cast<int>(
      std::lround((p.pos[1] - (input.xminLocal[1] + 0.5f * input.dy)) / input.dy));
    const int k = static_cast<int>(
      std::lround((p.pos[2] - input.xminLocal[2]) / input.dz));
    if (i < 0 || i >= input.width || j < 0 || j >= input.height ||
        k < 0 || k >= input.depth) {
      continue;
    }

    const float cx = input.xminLocal[0] + (static_cast<float>(i) + 0.5f) * input.dx;
    const float cy = input.xminLocal[1] + (static_cast<float>(j) + 0.5f) * input.dy;
    const float cz = input.xminLocal[2] + static_cast<float>(k) * input.dz;
    const float dx = p.pos[0] - cx;
    const float dy = p.pos[1] - cy;
    const float dz = p.pos[2] - cz;
    const float dist2 = dx * dx + dy * dy + dz * dz;
    const std::size_t idx = voxelIndex(i, j, k);
    if (dist2 < seedDist2[idx]) {
      seedDist2[idx] = dist2;
      seedLabels[idx] = static_cast<std::int32_t>(particleIndex);
    }
  }

  const std::size_t seededVoxelCount =
    static_cast<std::size_t>(std::count_if(seedLabels.begin(),
                                           seedLabels.end(),
                                           [](std::int32_t label) {
                                             return label >= 0;
                                           }));
  if (seededVoxelCount == 0) {
    std::cerr << "Metal Voronoi projection seed grid is empty." << std::endl;
    return false;
  }

  id<MTLBuffer> particleBuffer =
    [device newBufferWithBytes:input.particles.data()
                        length:particleBytes
                       options:MTLResourceStorageModeShared];
  id<MTLBuffer> labelA =
    [device newBufferWithBytes:seedLabels.data()
                        length:labelBytes
                       options:MTLResourceStorageModeShared];
  id<MTLBuffer> labelB =
    [device newBufferWithLength:labelBytes options:MTLResourceStorageModeShared];
  if (!particleBuffer || !labelA || !labelB) {
    std::cerr << "Metal Voronoi projection resource allocation failed."
              << std::endl;
    return false;
  }

  VoronoiUniformsCpu uniforms;
  uniforms.width = static_cast<std::uint32_t>(input.width);
  uniforms.height = static_cast<std::uint32_t>(input.height);
  uniforms.depth = static_cast<std::uint32_t>(input.depth);
  uniforms.particleCount = static_cast<std::uint32_t>(input.particles.size());
  uniforms.dx = input.dx;
  uniforms.dy = input.dy;
  uniforms.dz = input.dz;
  uniforms.xminX = input.xminLocal[0];
  uniforms.xminY = input.xminLocal[1];
  uniforms.xminZ = input.xminLocal[2];
  uniforms.densityWeight = input.densityWeight ? 1u : 0u;
  uniforms.renderColor[0] = input.renderColor[0];
  uniforms.renderColor[1] = input.renderColor[1];
  uniforms.renderColor[2] = input.renderColor[2];
  uniforms.colorMapSize = static_cast<std::uint32_t>(input.colorMapSize);
  uniforms.colorLogScale = input.colorLogScale ? 1u : 0u;
  uniforms.colorValueMin = input.colorValueMin;
  uniforms.colorValueMax = input.colorValueMax;

  id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
  if (!commandBuffer) {
    std::cerr << "Metal Voronoi projection command buffer creation failed."
              << std::endl;
    return false;
  }

  const auto start = std::chrono::steady_clock::now();
  const auto dispatch1D = [](id<MTLComputeCommandEncoder> encoder,
                             id<MTLComputePipelineState> pipeline,
                             std::size_t count) {
    const NSUInteger threadsPerGroup =
      std::min<NSUInteger>(pipeline.maxTotalThreadsPerThreadgroup,
                           std::max<NSUInteger>(pipeline.threadExecutionWidth,
                                               64u));
    [encoder dispatchThreads:MTLSizeMake(static_cast<NSUInteger>(count), 1, 1)
       threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];
  };

  id<MTLBuffer> currentLabels = labelA;
  id<MTLBuffer> nextLabels = labelB;
  int maxDim = std::max(input.width, std::max(input.height, input.depth));
  std::uint32_t step = 1u;
  while (step < static_cast<std::uint32_t>((maxDim + 1) / 2)) {
    step <<= 1u;
  }
  std::vector<std::uint32_t> steps;
  for (; step >= 1u; step >>= 1u) {
    steps.push_back(step);
    if (step == 1u) {
      break;
    }
  }
  steps.push_back(1u);
  steps.push_back(1u);

  for (std::uint32_t passStep : steps) {
    id<MTLComputeCommandEncoder> encoder =
      [commandBuffer computeCommandEncoder];
    [encoder setComputePipelineState:jfaPipeline];
    [encoder setBuffer:particleBuffer offset:0 atIndex:0];
    [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setBuffer:currentLabels offset:0 atIndex:2];
    [encoder setBuffer:nextLabels offset:0 atIndex:3];
    [encoder setBytes:&passStep length:sizeof(passStep) atIndex:4];
    dispatch1D(encoder, jfaPipeline, voxelCount);
    [encoder endEncoding];
    std::swap(currentLabels, nextLabels);
  }

  [commandBuffer commit];
  [commandBuffer waitUntilCompleted];
  const auto end = std::chrono::steady_clock::now();

  if (commandBuffer.status == MTLCommandBufferStatusError) {
    std::cerr << "Metal Voronoi projection command failed: "
              << (commandBuffer.error
                    ? [[commandBuffer.error localizedDescription] UTF8String]
                    : "unknown error")
              << std::endl;
    return false;
  }

  grid.width = input.width;
  grid.height = input.height;
  grid.depth = input.depth;
  grid.labels.resize(voxelCount);
  const auto* labels = static_cast<const std::int32_t*>([currentLabels contents]);
  std::copy(labels, labels + voxelCount, grid.labels.begin());
  grid.elapsedMs =
    std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "Metal Voronoi JFA labels: particles="
            << input.particles.size()
            << " grid=" << input.width << "x" << input.height << "x"
            << input.depth
            << " seededVoxels=" << seededVoxelCount
            << " elapsedMs=" << grid.elapsedMs
            << std::endl;
  return true;
}

bool IntegrateMetalVoronoiLabelGrid(const MetalProjectionMapInput& input,
                                    const MetalVoronoiLabelGrid& grid,
                                    MetalProjectionMapOutput& output)
{
  output = MetalProjectionMapOutput{};
  if (input.width <= 0 || input.height <= 0 || input.depth <= 0 ||
      input.width != grid.width || input.height != grid.height ||
      input.depth != grid.depth || input.particles.empty() ||
      grid.labels.empty()) {
    return false;
  }

  static id<MTLDevice> device = CreateProjectionDevice();
  static id<MTLCommandQueue> queue = device ? [device newCommandQueue] : nil;
  static id<MTLComputePipelineState> integratePipeline = nil;
  if (!device || !queue ||
      !CompileNamedPipeline(device, @"voronoiIntegrate", integratePipeline)) {
    std::cerr << "Metal Voronoi integration setup failed." << std::endl;
    return false;
  }

  const std::size_t pixelCount =
    static_cast<std::size_t>(input.width) *
    static_cast<std::size_t>(input.height);
  const std::size_t voxelCount = pixelCount * static_cast<std::size_t>(input.depth);
  const std::size_t particleBytes =
    input.particles.size() * sizeof(MetalProjectionParticle);
  const std::size_t labelBytes = voxelCount * sizeof(std::int32_t);
  const std::size_t outputBytes = pixelCount * sizeof(float);

  id<MTLBuffer> particleBuffer =
    [device newBufferWithBytes:input.particles.data()
                        length:particleBytes
                       options:MTLResourceStorageModeShared];
  id<MTLBuffer> labelBuffer =
    [device newBufferWithBytes:grid.labels.data()
                        length:labelBytes
                       options:MTLResourceStorageModeShared];
  id<MTLBuffer> valueBuffer =
    [device newBufferWithLength:outputBytes options:MTLResourceStorageModeShared];
  id<MTLBuffer> weightBuffer =
    [device newBufferWithLength:outputBytes options:MTLResourceStorageModeShared];
  if (!particleBuffer || !labelBuffer || !valueBuffer || !weightBuffer) {
    std::cerr << "Metal Voronoi integration resource allocation failed."
              << std::endl;
    return false;
  }

  VoronoiUniformsCpu uniforms;
  uniforms.width = static_cast<std::uint32_t>(input.width);
  uniforms.height = static_cast<std::uint32_t>(input.height);
  uniforms.depth = static_cast<std::uint32_t>(input.depth);
  uniforms.particleCount = static_cast<std::uint32_t>(input.particles.size());
  uniforms.dx = input.dx;
  uniforms.dy = input.dy;
  uniforms.dz = input.dz;
  uniforms.xminX = input.xminLocal[0];
  uniforms.xminY = input.xminLocal[1];
  uniforms.xminZ = input.xminLocal[2];
  uniforms.densityWeight = input.densityWeight ? 1u : 0u;

  id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
  id<MTLComputeCommandEncoder> integrateEncoder =
    [commandBuffer computeCommandEncoder];
  if (!commandBuffer || !integrateEncoder) {
    return false;
  }

  const auto start = std::chrono::steady_clock::now();
  const auto dispatch1D = [](id<MTLComputeCommandEncoder> encoder,
                             id<MTLComputePipelineState> pipeline,
                             std::size_t count) {
    const NSUInteger threadsPerGroup =
      std::min<NSUInteger>(pipeline.maxTotalThreadsPerThreadgroup,
                           std::max<NSUInteger>(pipeline.threadExecutionWidth,
                                               64u));
    [encoder dispatchThreads:MTLSizeMake(static_cast<NSUInteger>(count), 1, 1)
       threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];
  };

  [integrateEncoder setComputePipelineState:integratePipeline];
  [integrateEncoder setBuffer:particleBuffer offset:0 atIndex:0];
  [integrateEncoder setBytes:&uniforms length:sizeof(uniforms) atIndex:1];
  [integrateEncoder setBuffer:labelBuffer offset:0 atIndex:2];
  [integrateEncoder setBuffer:valueBuffer offset:0 atIndex:3];
  [integrateEncoder setBuffer:weightBuffer offset:0 atIndex:4];
  dispatch1D(integrateEncoder, integratePipeline, pixelCount);
  [integrateEncoder endEncoding];
  [commandBuffer commit];
  [commandBuffer waitUntilCompleted];
  const auto end = std::chrono::steady_clock::now();

  if (commandBuffer.status == MTLCommandBufferStatusError) {
    std::cerr << "Metal Voronoi integration command failed: "
              << (commandBuffer.error
                    ? [[commandBuffer.error localizedDescription] UTF8String]
                    : "unknown error")
              << std::endl;
    return false;
  }

  output.values.resize(pixelCount);
  output.weights.resize(pixelCount);
  const auto* sumValues = static_cast<const float*>([valueBuffer contents]);
  const auto* sumWeights = static_cast<const float*>([weightBuffer contents]);
  std::size_t nonzeroWeights = 0;
  double weightSum = 0.0;
  float weightMax = 0.0f;
  for (std::size_t i = 0; i < pixelCount; ++i) {
    output.values[i] = sumValues[i];
    output.weights[i] = sumWeights[i];
    if (output.weights[i] > 0.0f) {
      ++nonzeroWeights;
      weightSum += output.weights[i];
      weightMax = std::max(weightMax, output.weights[i]);
    }
  }
  output.elapsedMs =
    std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "Metal Voronoi integration: particles="
            << input.particles.size()
            << " grid=" << input.width << "x" << input.height << "x"
            << input.depth
            << " nonzeroWeights=" << nonzeroWeights
            << " weightSum=" << weightSum
            << " weightMax=" << weightMax
            << " elapsedMs=" << output.elapsedMs
            << std::endl;
  if (nonzeroWeights == 0) {
    std::cerr << "Metal Voronoi projection produced an empty output."
              << std::endl;
    return false;
  }
  return true;
}

bool RenderMetalVoronoiLabelGrid(const MetalProjectionMapInput& input,
                                 const MetalVoronoiLabelGrid& grid,
                                 MetalProjectionMapOutput& output)
{
  output = MetalProjectionMapOutput{};
  if (input.width <= 0 || input.height <= 0 || input.depth <= 0 ||
      input.width != grid.width || input.height != grid.height ||
      input.depth != grid.depth || input.particles.empty() ||
      grid.labels.empty() || input.transferComponents.empty()) {
    return false;
  }

  static id<MTLDevice> device = CreateProjectionDevice();
  static id<MTLCommandQueue> queue = device ? [device newCommandQueue] : nil;
  static id<MTLComputePipelineState> renderPipeline = nil;
  if (!device || !queue ||
      !CompileNamedPipeline(device, @"voronoiRender", renderPipeline)) {
    std::cerr << "Metal Voronoi render setup failed." << std::endl;
    return false;
  }

  const std::size_t pixelCount =
    static_cast<std::size_t>(input.width) *
    static_cast<std::size_t>(input.height);
  const std::size_t voxelCount = pixelCount * static_cast<std::size_t>(input.depth);
  const std::size_t particleBytes =
    input.particles.size() * sizeof(MetalProjectionParticle);
  const std::size_t labelBytes = voxelCount * sizeof(std::int32_t);
  const std::size_t rgbBytes = pixelCount * 3 * sizeof(float);
  const std::size_t alphaBytes = pixelCount * sizeof(float);

  std::vector<ProjectionTfComponentCpu> tfComponents(
    std::min<std::size_t>(input.transferComponents.size(),
                          kProjectionGpuMaxTfComponents));
  for (std::size_t i = 0; i < tfComponents.size(); ++i) {
    const ProjectionGpuTransferComponent& src = input.transferComponents[i];
    tfComponents[i].type = static_cast<std::uint32_t>(std::clamp(src.type, 0, 2));
    tfComponents[i].center = src.center;
    tfComponents[i].width = src.width;
    tfComponents[i].amplitude = src.amplitude;
    tfComponents[i].logDomain = src.logDomain ? 1u : 0u;
  }

  id<MTLBuffer> particleBuffer =
    [device newBufferWithBytes:input.particles.data()
                        length:particleBytes
                       options:MTLResourceStorageModeShared];
  id<MTLBuffer> labelBuffer =
    [device newBufferWithBytes:grid.labels.data()
                        length:labelBytes
                       options:MTLResourceStorageModeShared];
  id<MTLBuffer> rgbBuffer =
    [device newBufferWithLength:rgbBytes options:MTLResourceStorageModeShared];
  id<MTLBuffer> alphaBuffer =
    [device newBufferWithLength:alphaBytes options:MTLResourceStorageModeShared];
  id<MTLBuffer> tfBuffer =
    [device newBufferWithBytes:tfComponents.data()
                        length:tfComponents.size() * sizeof(ProjectionTfComponentCpu)
                       options:MTLResourceStorageModeShared];
  id<MTLBuffer> colorMapBuffer =
    [device newBufferWithBytes:input.colorMap.data()
                        length:input.colorMap.size() * sizeof(float)
                       options:MTLResourceStorageModeShared];
  if (!particleBuffer || !labelBuffer || !rgbBuffer || !alphaBuffer ||
      !tfBuffer || !colorMapBuffer) {
    std::cerr << "Metal Voronoi render resource allocation failed."
              << std::endl;
    return false;
  }

  VoronoiUniformsCpu uniforms;
  uniforms.width = static_cast<std::uint32_t>(input.width);
  uniforms.height = static_cast<std::uint32_t>(input.height);
  uniforms.depth = static_cast<std::uint32_t>(input.depth);
  uniforms.particleCount = static_cast<std::uint32_t>(input.particles.size());
  uniforms.dx = input.dx;
  uniforms.dy = input.dy;
  uniforms.dz = input.dz;
  uniforms.xminX = input.xminLocal[0];
  uniforms.xminY = input.xminLocal[1];
  uniforms.xminZ = input.xminLocal[2];
  uniforms.densityWeight = input.densityWeight ? 1u : 0u;
  uniforms.renderColor[0] = input.renderColor[0];
  uniforms.renderColor[1] = input.renderColor[1];
  uniforms.renderColor[2] = input.renderColor[2];
  uniforms.tfCount = static_cast<std::uint32_t>(tfComponents.size());
  uniforms.colorMapSize = static_cast<std::uint32_t>(input.colorMapSize);
  uniforms.colorLogScale = input.colorLogScale ? 1u : 0u;
  uniforms.colorValueMin = input.colorValueMin;
  uniforms.colorValueMax = input.colorValueMax;

  id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
  id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
  if (!commandBuffer || !encoder) {
    return false;
  }

  const auto start = std::chrono::steady_clock::now();
  const auto dispatch1D = [](id<MTLComputeCommandEncoder> encoder,
                             id<MTLComputePipelineState> pipeline,
                             std::size_t count) {
    const NSUInteger threadsPerGroup =
      std::min<NSUInteger>(pipeline.maxTotalThreadsPerThreadgroup,
                           std::max<NSUInteger>(pipeline.threadExecutionWidth,
                                               64u));
    [encoder dispatchThreads:MTLSizeMake(static_cast<NSUInteger>(count), 1, 1)
       threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];
  };

  [encoder setComputePipelineState:renderPipeline];
  [encoder setBuffer:particleBuffer offset:0 atIndex:0];
  [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:1];
  [encoder setBuffer:labelBuffer offset:0 atIndex:2];
  [encoder setBuffer:rgbBuffer offset:0 atIndex:3];
  [encoder setBuffer:alphaBuffer offset:0 atIndex:4];
  [encoder setBuffer:tfBuffer offset:0 atIndex:5];
  [encoder setBuffer:colorMapBuffer offset:0 atIndex:6];
  dispatch1D(encoder, renderPipeline, pixelCount);
  [encoder endEncoding];
  [commandBuffer commit];
  [commandBuffer waitUntilCompleted];
  const auto end = std::chrono::steady_clock::now();

  if (commandBuffer.status == MTLCommandBufferStatusError) {
    std::cerr << "Metal Voronoi render command failed: "
              << (commandBuffer.error
                    ? [[commandBuffer.error localizedDescription] UTF8String]
                    : "unknown error")
              << std::endl;
    return false;
  }

  output.rgb.resize(pixelCount * 3);
  output.weights.resize(pixelCount);
  const auto* rgb = static_cast<const float*>([rgbBuffer contents]);
  const auto* alpha = static_cast<const float*>([alphaBuffer contents]);
  std::copy(rgb, rgb + pixelCount * 3, output.rgb.begin());
  std::copy(alpha, alpha + pixelCount, output.weights.begin());
  output.elapsedMs =
    std::chrono::duration<double, std::milli>(end - start).count();

  std::size_t nonzeroAlpha = 0;
  double alphaSum = 0.0;
  for (float a : output.weights) {
    if (a > 0.0f) {
      ++nonzeroAlpha;
      alphaSum += a;
    }
  }
  std::cout << "Metal Voronoi opacity render: particles="
            << input.particles.size()
            << " grid=" << input.width << "x" << input.height << "x"
            << input.depth
            << " nonzeroAlpha=" << nonzeroAlpha
            << " alphaSum=" << alphaSum
            << " elapsedMs=" << output.elapsedMs
            << std::endl;
  return true;
}

bool RunMetalVoronoiProjectionMap(const MetalProjectionMapInput& input,
                                  MetalProjectionMapOutput& output)
{
  MetalVoronoiLabelGrid grid;
  if (!BuildMetalVoronoiLabelGrid(input, grid)) {
    return false;
  }
  if (!IntegrateMetalVoronoiLabelGrid(input, grid, output)) {
    return false;
  }
  output.elapsedMs += grid.elapsedMs;
  return true;
}
