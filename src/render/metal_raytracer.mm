#include "render/metal_raytracer.h"
#include "game/world.h"
#include "render/renderer.h"

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <simd/simd.h>

#include <vector>
#include <cstring>

namespace MetalRT {

struct PackedVertex {
  simd_float3 position;
  uint32_t packed_normal;
  simd_float2 texCoord;
  uint32_t packed_color;
};

struct GPUWorldLight {
  simd_float3 direction;
  float _pad0;
  simd_float3 ambient;
  float _pad1;
  simd_float3 diffuse;
  float _pad2;
};

struct GPUCamera {
  simd_float4x4 viewMatrix;
  simd_float4x4 projMatrix;
  simd_float3 position;
  uint32_t frameIndex;
};

struct MetalRaytracer::Impl {
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> commandQueue = nil;
  id<MTLLibrary> shaderLibrary = nil;
  
  id<MTLComputePipelineState> raytracePipeline = nil;
  id<MTLComputePipelineState> accumulatePipeline = nil;
  id<MTLComputePipelineState> denoisePipeline = nil;
  
  id<MTLBuffer> geometryBuffer = nil;
  id<MTLBuffer> indexBuffer = nil;
  id<MTLBuffer> lightBuffer = nil;
  id<MTLBuffer> cameraBuffer = nil;
  id<MTLBuffer> accumulationBuffer = nil;
  
  id<MTLTexture> renderTarget = nil;
  id<MTLTexture> accumulationTexture = nil;
  
  dispatch_semaphore_t inflightSemaphore = nil;
  
  int windowWidth = 0;
  int windowHeight = 0;
  uint32_t frameCount = 0;
  
  std::vector<PackedVertex> vertices;
  std::vector<uint32_t> indices;
  uint32_t totalTriangles = 0;
};

static const char* kMetalShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct Vertex {
  float3 position;
  float3 normal;
  float2 texCoord;
  float3 color;
};

struct Ray {
  float3 origin;
  float3 direction;
};

struct RayPayload {
  float3 color;
  uint depth;
};

struct WorldLight {
  float3 direction;
  float3 ambient;
  float3 diffuse;
};

struct Camera {
  float4x4 viewMatrix;
  float4x4 projMatrix;
  float3 position;
  uint frameIndex;
};

kernel void raytraceKernel(
    uint2 tid [[thread_position_in_grid]],
    texture2d<float, access::write> outputTexture [[texture(0)]],
    constant Camera& camera [[buffer(0)]],
    constant WorldLight& light [[buffer(1)]]) {
  
  if (tid.x >= outputTexture.get_width() || tid.y >= outputTexture.get_height()) {
    return;
  }
  
  float width = float(outputTexture.get_width());
  float height = float(outputTexture.get_height());
  float2 uv = float2(tid) / float2(width, height);
  
  float3 rayOrigin = camera.position;
  float3 rayDir = normalize(float3(
    (uv.x - 0.5) * 2.0,
    (0.5 - uv.y) * 2.0,
    -1.0
  ));
  
  float3 color = light.ambient;
  
  float brightness = abs(rayDir.z) * 0.5 + 0.5;
  color += light.diffuse * brightness;
  
  uint frame = camera.frameIndex;
  float jitter = fract(sin(float(frame) * 12.9898) * 43758.5453);
  color = mix(color, float3(jitter, jitter, jitter) * 0.2, 0.1);
  
  outputTexture.write(float4(color, 1.0), tid);
}

kernel void accumulateKernel(
    uint2 tid [[thread_position_in_grid]],
    texture2d<float, access::read> inputTexture [[texture(0)]],
    texture2d<float, access::read_write> accumulationTexture [[texture(1)]],
    constant uint& frameCount [[buffer(0)]]) {
  
  if (tid.x >= inputTexture.get_width() || tid.y >= inputTexture.get_height()) {
    return;
  }
  
  float4 current = inputTexture.read(tid);
  float4 accumulated = accumulationTexture.read(tid);
  
  float alpha = 1.0 / float(frameCount + 1);
  float4 result = mix(accumulated, current, alpha);
  
  accumulationTexture.write(result, tid);
}

kernel void denoiseKernel(
    uint2 tid [[thread_position_in_grid]],
    texture2d<float, access::read> inputTexture [[texture(0)]],
    texture2d<float, access::write> outputTexture [[texture(1)]]) {
  
  if (tid.x >= inputTexture.get_width() || tid.y >= inputTexture.get_height()) {
    return;
  }
  
  float3 center = inputTexture.read(tid).rgb;
  float3 filtered = center;
  
  int2 offsets[4] = {
    int2(1, 0), int2(-1, 0),
    int2(0, 1), int2(0, -1)
  };
  
  float sumWeight = 1.0;
  for (int i = 0; i < 4; i++) {
    uint2 samplePos = uint2(int2(tid) + offsets[i]);
    if (samplePos.x < inputTexture.get_width() && samplePos.y < inputTexture.get_height()) {
      float3 sample = inputTexture.read(samplePos).rgb;
      float3 diff = sample - center;
      float weight = exp(-dot(diff, diff) * 4.0);
      filtered += sample * weight;
      sumWeight += weight;
    }
  }
  
  filtered /= sumWeight;
  outputTexture.write(float4(filtered, 1.0), tid);
}
)";

MetalRaytracer::MetalRaytracer()
    : m_impl(std::make_unique<Impl>()) {
}

MetalRaytracer::~MetalRaytracer() {
  shutdown();
}

bool MetalRaytracer::initialize(int windowWidth, int windowHeight) {
  auto& impl = *m_impl;
  
  impl.device = MTLCreateSystemDefaultDevice();
  if (!impl.device) {
    fprintf(stderr, "Failed to get Metal device\n");
    return false;
  }
  
  fprintf(stderr, "Metal Device: %s\n", [[impl.device name] UTF8String]);
  
  impl.commandQueue = [impl.device newCommandQueue];
  impl.commandQueue.label = @"RayTracingQueue";
  if (!impl.commandQueue) {
    fprintf(stderr, "Failed to create Metal command queue\n");
    return false;
  }
  
  NSError* error = nil;
  NSString* shaderSource = [NSString stringWithUTF8String:kMetalShaderSource];
  MTLCompileOptions* options = [MTLCompileOptions new];
  options.languageVersion = MTLLanguageVersion3_0;
  if (@available(macOS 15.0, *)) {
    options.mathMode = MTLMathModeRelaxed;
  } else {
    options.fastMathEnabled = YES;
  }
  
  impl.shaderLibrary = [impl.device newLibraryWithSource:shaderSource
                                                 options:options
                                                   error:&error];
  [options release];
  
  if (!impl.shaderLibrary) {
    fprintf(stderr, "Failed to compile Metal shader library: %s\n",
            [[error description] UTF8String]);
    return false;
  }
  
  fprintf(stderr, "Shader library compiled successfully\n");
  
  id<MTLFunction> raytraceFunc = [impl.shaderLibrary newFunctionWithName:@"raytraceKernel"];
  if (!raytraceFunc) {
    fprintf(stderr, "Failed to get raytraceKernel function\n");
    return false;
  }
  
  MTLComputePipelineDescriptor* pipelineDesc = [MTLComputePipelineDescriptor new];
  pipelineDesc.computeFunction = raytraceFunc;
  pipelineDesc.threadGroupSizeIsMultipleOfThreadExecutionWidth = YES;
  
  impl.raytracePipeline = [impl.device newComputePipelineStateWithDescriptor:pipelineDesc
                                                                     options:MTLPipelineOptionNone
                                                                 reflection:nil
                                                                      error:&error];
  [pipelineDesc release];
  [raytraceFunc release];
  
  if (!impl.raytracePipeline) {
    fprintf(stderr, "Failed to create raytracing pipeline: %s\n",
            [[error description] UTF8String]);
    return false;
  }
  
  fprintf(stderr, "Ray tracing pipeline created\n");
  
  id<MTLFunction> accumulateFunc = [impl.shaderLibrary newFunctionWithName:@"accumulateKernel"];
  if (accumulateFunc) {
    pipelineDesc = [MTLComputePipelineDescriptor new];
    pipelineDesc.computeFunction = accumulateFunc;
    impl.accumulatePipeline = [impl.device newComputePipelineStateWithDescriptor:pipelineDesc
                                                                          options:MTLPipelineOptionNone
                                                                      reflection:nil
                                                                           error:&error];
    [pipelineDesc release];
    [accumulateFunc release];
  }
  
  id<MTLFunction> denoiseFunc = [impl.shaderLibrary newFunctionWithName:@"denoiseKernel"];
  if (denoiseFunc) {
    pipelineDesc = [MTLComputePipelineDescriptor new];
    pipelineDesc.computeFunction = denoiseFunc;
    impl.denoisePipeline = [impl.device newComputePipelineStateWithDescriptor:pipelineDesc
                                                                       options:MTLPipelineOptionNone
                                                                   reflection:nil
                                                                        error:&error];
    [pipelineDesc release];
    [denoiseFunc release];
  }
  
  impl.inflightSemaphore = dispatch_semaphore_create(2);
  
  impl.windowWidth = windowWidth;
  impl.windowHeight = windowHeight;
  
  fprintf(stderr, "Metal ray tracer initialized successfully (resolution: %dx%d)\n",
          windowWidth, windowHeight);
  return true;
}

void MetalRaytracer::shutdown() {
  auto& impl = *m_impl;
  
  if (impl.geometryBuffer) [impl.geometryBuffer release];
  if (impl.indexBuffer) [impl.indexBuffer release];
  if (impl.lightBuffer) [impl.lightBuffer release];
  if (impl.cameraBuffer) [impl.cameraBuffer release];
  if (impl.accumulationBuffer) [impl.accumulationBuffer release];
  
  if (impl.renderTarget) [impl.renderTarget release];
  if (impl.accumulationTexture) [impl.accumulationTexture release];
  
  if (impl.raytracePipeline) [impl.raytracePipeline release];
  if (impl.accumulatePipeline) [impl.accumulatePipeline release];
  if (impl.denoisePipeline) [impl.denoisePipeline release];
  
  if (impl.shaderLibrary) [impl.shaderLibrary release];
  
  if (impl.commandQueue) [impl.commandQueue release];
  
  if (impl.device) [impl.device release];
  
  if (impl.inflightSemaphore) {
    dispatch_release(impl.inflightSemaphore);
    impl.inflightSemaphore = nil;
  }
}

void MetalRaytracer::uploadWorldMeshes(const World& world) {
  auto& impl = *m_impl;
  if (!impl.device) return;
  
  impl.vertices.clear();
  impl.indices.clear();
  impl.totalTriangles = 0;
  
  uint32_t currentIndexOffset = 0;
  for (const ChunkCoord& coord : world.visibleChunks) {
    const Chunk* chunk = findChunk(world, coord.cx, coord.cy, coord.cz);
    if (!chunk || chunk->vertices.empty() || chunk->indices.empty()) {
      continue;
    }
    
    for (const Vertex& v : chunk->vertices) {
      PackedVertex pv;
      pv.position = simd_make_float3(v.x * 0.0625f, v.y * 0.0625f, v.z * 0.0625f);
      pv.texCoord = simd_make_float2(v.u * 0.0625f, v.v * 0.0625f);
      pv.packed_normal = (v.nx & 0xFF) | ((v.ny & 0xFF) << 8) | ((v.nz & 0xFF) << 16);
      pv.packed_color = (v.r) | (v.g << 8) | (v.b << 16) | (0xFF << 24);
      impl.vertices.push_back(pv);
    }
    
    for (uint32_t idx : chunk->indices) {
      impl.indices.push_back(idx + currentIndexOffset);
    }
    
    impl.totalTriangles += chunk->indices.size() / 3;
    currentIndexOffset += chunk->vertices.size();
  }
  
  if (impl.vertices.empty() || impl.indices.empty()) {
    return;
  }
  
  if (impl.geometryBuffer) {
    [impl.geometryBuffer release];
  }
  impl.geometryBuffer = [impl.device newBufferWithBytes:impl.vertices.data()
                                                 length:impl.vertices.size() * sizeof(PackedVertex)
                                                options:MTLResourceStorageModeShared];
  
  if (impl.indexBuffer) {
    [impl.indexBuffer release];
  }
  impl.indexBuffer = [impl.device newBufferWithBytes:impl.indices.data()
                                              length:impl.indices.size() * sizeof(uint32_t)
                                             options:MTLResourceStorageModeShared];
  
  fprintf(stderr, "Uploaded %u vertices and %u indices to GPU\n",
          static_cast<uint32_t>(impl.vertices.size()),
          static_cast<uint32_t>(impl.indices.size()));
}

void MetalRaytracer::renderFrame(const Vec3& cameraPos, const Mat4& viewProj,
                                 const WorldLight& light, float timeNow) {
  auto& impl = *m_impl;
  if (!impl.device || !impl.raytracePipeline) return;
  
  dispatch_semaphore_wait(impl.inflightSemaphore, DISPATCH_TIME_FOREVER);
  
  id<MTLCommandBuffer> cmdBuffer = [impl.commandQueue commandBuffer];
  cmdBuffer.label = @"RaytraceFrame";
  
  __block dispatch_semaphore_t semaphore = impl.inflightSemaphore;
  [cmdBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
    dispatch_semaphore_signal(semaphore);
  }];
  
  id<MTLComputeCommandEncoder> computeEncoder = [cmdBuffer computeCommandEncoder];
  computeEncoder.label = @"RaytracePass";
  
  if (!impl.renderTarget || (int)impl.renderTarget.width != impl.windowWidth ||
      (int)impl.renderTarget.height != impl.windowHeight) {
    if (impl.renderTarget) [impl.renderTarget release];
    
    MTLTextureDescriptor* textureDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                    width:impl.windowWidth
                                   height:impl.windowHeight
                                mipmapped:NO];
    textureDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    textureDesc.storageMode = MTLStorageModePrivate;
    
    impl.renderTarget = [impl.device newTextureWithDescriptor:textureDesc];
    impl.renderTarget.label = @"RaytraceOutput";
  }
  
  if (!impl.cameraBuffer) {
    impl.cameraBuffer = [impl.device newBufferWithLength:sizeof(GPUCamera)
                                                 options:MTLResourceStorageModeShared];
    impl.cameraBuffer.label = @"CameraBuffer";
  }
  
  GPUCamera* cameraPtr = (GPUCamera*)impl.cameraBuffer.contents;
  cameraPtr->position = simd_make_float3(cameraPos.x, cameraPos.y, cameraPos.z);
  cameraPtr->frameIndex = impl.frameCount++;
  
  if (!impl.lightBuffer) {
    impl.lightBuffer = [impl.device newBufferWithLength:sizeof(GPUWorldLight)
                                                options:MTLResourceStorageModeShared];
    impl.lightBuffer.label = @"LightBuffer";
  }
  
  GPUWorldLight* lightPtr = (GPUWorldLight*)impl.lightBuffer.contents;
  lightPtr->direction = simd_make_float3(light.direction.x, light.direction.y, light.direction.z);
  lightPtr->ambient = simd_make_float3(light.ambient.x, light.ambient.y, light.ambient.z);
  lightPtr->diffuse = simd_make_float3(light.diffuse.x, light.diffuse.y, light.diffuse.z);
  
  [computeEncoder setComputePipelineState:impl.raytracePipeline];
  [computeEncoder setTexture:impl.renderTarget atIndex:0];
  [computeEncoder setBuffer:impl.cameraBuffer offset:0 atIndex:0];
  [computeEncoder setBuffer:impl.lightBuffer offset:0 atIndex:1];
  
  NSUInteger maxThreadsPerGroup = impl.raytracePipeline.maxTotalThreadsPerThreadgroup;
  NSUInteger threadGroupWidth = 8;
  NSUInteger threadGroupHeight = 8;
  
  if (threadGroupWidth * threadGroupHeight > maxThreadsPerGroup) {
    threadGroupWidth = 8;
    threadGroupHeight = 4;
  }
  
  MTLSize threadgroupSize = MTLSizeMake(threadGroupWidth, threadGroupHeight, 1);
  MTLSize gridSize = MTLSizeMake(impl.windowWidth, impl.windowHeight, 1);
  
  [computeEncoder dispatchThreads:gridSize threadsPerThreadgroup:threadgroupSize];
  [computeEncoder endEncoding];
  [cmdBuffer commit];
}

uint32_t MetalRaytracer::getOutputTexture() const {
  return reinterpret_cast<uintptr_t>(m_impl->renderTarget);
}

void MetalRaytracer::setConfig(const RaytracerConfig& config) {
  m_config = config;
}

void MetalRaytracer::resize(int windowWidth, int windowHeight) {
  m_impl->windowWidth = windowWidth;
  m_impl->windowHeight = windowHeight;
}

} // namespace MetalRT
