/*
 * SPDX-License-Identifier: MIT OR Apache-2.0
 *
 * Headless compute check adapted from gfx-rs/wgpu-native examples/compute
 * at 6aed50955d934ac36049ba8d002034841633ae02.
 * Upstream license texts are retained in ../upstream/LICENSE.*.
 *
 * The WGSL is embedded so the executable does not depend on a working
 * directory or a separate shader file. Runtime checks are not compiled
 * out by NDEBUG.
 */
#define _POSIX_C_SOURCE 200809L
#include "wgpu.h"
#include <cosmo.h>
#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EXPECTED_VERSION UINT32_C(0x1d000101)
#define CALLBACK_TIMEOUT_MS UINT64_C(30000)

enum {
  CHECK_OK = 0,
  CHECK_USAGE = 2,
  CHECK_VERSION = 3,
  CHECK_INITIALIZATION = 4,
  CHECK_EXECUTION = 5,
  CHECK_TIMEOUT = 6,
  CHECK_READBACK = 7,
};

static const char shader[] =
    "@group(0) @binding(0)\n"
    "var<storage, read_write> numbers: array<u32>;\n"
    "fn collatz_iterations(input: u32) -> u32 {\n"
    "  var n = input;\n"
    "  var count = 0u;\n"
    "  loop {\n"
    "    if (n <= 1u) { break; }\n"
    "    if (n % 2u == 0u) {\n"
    "      n = n / 2u;\n"
    "    } else {\n"
    "      if (n >= 1431655765u) { return 4294967295u; }\n"
    "      n = 3u * n + 1u;\n"
    "    }\n"
    "    count = count + 1u;\n"
    "  }\n"
    "  return count;\n"
    "}\n"
    "@compute @workgroup_size(1)\n"
    "fn main(@builtin(global_invocation_id) id: vec3<u32>) {\n"
    "  if (id.x < arrayLength(&numbers)) {\n"
    "    numbers[id.x] = collatz_iterations(numbers[id.x]);\n"
    "  }\n"
    "}\n";

struct AdapterRequest {
  atomic_int done;
  WGPURequestAdapterStatus status;
  WGPUAdapter adapter;
};

struct DeviceRequest {
  atomic_int done;
  WGPURequestDeviceStatus status;
  WGPUDevice device;
};

struct MapRequest {
  atomic_int done;
  WGPUMapAsyncStatus status;
};

static atomic_int execution_error;
static bool trace_enabled;

static void trace_stage(const char *stage) {
  if (trace_enabled) {
    fprintf(stderr, "compute stage: %s\n", stage);
    fflush(stderr);
  }
}

static void print_view(FILE *stream, WGPUStringView message) {
  if (message.data) {
    size_t length = message.length == WGPU_STRLEN
                        ? strlen(message.data)
                        : message.length;
    fwrite(message.data, 1, length, stream);
  }
}

static void print_error(const char *operation, WGPUStringView message) {
  fprintf(stderr, "%s: ", operation);
  print_view(stderr, message);
  fputc('\n', stderr);
}

static void log_message(WGPULogLevel level, WGPUStringView message,
                        void *userdata) {
  (void)userdata;
  fprintf(stderr, "wgpu log %u: ", (unsigned)level);
  print_view(stderr, message);
  fputc('\n', stderr);
}

static void adapter_ready(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                          WGPUStringView message, void *userdata1,
                          void *userdata2) {
  struct AdapterRequest *request = userdata1;
  (void)userdata2;
  request->status = status;
  request->adapter = adapter;
  if (status != WGPURequestAdapterStatus_Success)
    print_error("request adapter", message);
  atomic_store_explicit(&request->done, 1, memory_order_release);
}

static void device_ready(WGPURequestDeviceStatus status, WGPUDevice device,
                         WGPUStringView message, void *userdata1,
                         void *userdata2) {
  struct DeviceRequest *request = userdata1;
  (void)userdata2;
  request->status = status;
  request->device = device;
  if (status != WGPURequestDeviceStatus_Success)
    print_error("request device", message);
  atomic_store_explicit(&request->done, 1, memory_order_release);
}

static void buffer_mapped(WGPUMapAsyncStatus status, WGPUStringView message,
                          void *userdata1, void *userdata2) {
  struct MapRequest *request = userdata1;
  (void)userdata2;
  request->status = status;
  if (status != WGPUMapAsyncStatus_Success)
    print_error("map readback buffer", message);
  atomic_store_explicit(&request->done, 1, memory_order_release);
}

static void uncaptured_error(WGPUDevice const *device, WGPUErrorType type,
                             WGPUStringView message, void *userdata1,
                             void *userdata2) {
  (void)device;
  (void)userdata1;
  (void)userdata2;
  fprintf(stderr, "WebGPU error %u: ", (unsigned)type);
  print_view(stderr, message);
  fputc('\n', stderr);
  atomic_store_explicit(&execution_error, 1, memory_order_release);
}

static void device_lost(WGPUDevice const *device, WGPUDeviceLostReason reason,
                        WGPUStringView message, void *userdata1,
                        void *userdata2) {
  (void)device;
  (void)userdata1;
  (void)userdata2;
  if (reason != WGPUDeviceLostReason_Destroyed &&
      reason != WGPUDeviceLostReason_CallbackCancelled) {
    print_error("device lost", message);
    atomic_store_explicit(&execution_error, 1, memory_order_release);
  }
}

static int monotonic_ms(uint64_t *value) {
  struct timespec time;
  if (clock_gettime(CLOCK_MONOTONIC, &time)) {
    perror("clock_gettime");
    return CHECK_EXECUTION;
  }
  *value = (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
  return CHECK_OK;
}

static int wait_callback(WGPUInstance instance, WGPUDevice device,
                         atomic_int *done, const char *operation) {
  const struct timespec pause = {0, 1000000};
  uint64_t start, now;
  if (monotonic_ms(&start))
    return CHECK_EXECUTION;
  for (;;) {
    if (atomic_load_explicit(&execution_error, memory_order_acquire))
      return CHECK_EXECUTION;
    if (atomic_load_explicit(done, memory_order_acquire))
      return CHECK_OK;
    if (device)
      (void)wgpuDevicePoll(device, false, NULL);
    else
      wgpuInstanceProcessEvents(instance);
    if (atomic_load_explicit(done, memory_order_acquire))
      continue;
    if (monotonic_ms(&now))
      return CHECK_EXECUTION;
    if (now - start >= CALLBACK_TIMEOUT_MS) {
      fprintf(stderr, "%s: callback timed out after 30 seconds\n", operation);
      return CHECK_TIMEOUT;
    }
    if (nanosleep(&pause, NULL) && errno != EINTR) {
      perror("nanosleep");
      return CHECK_EXECUTION;
    }
  }
}

static const char *adapter_kind(WGPUAdapterType type) {
  switch (type) {
  case WGPUAdapterType_DiscreteGPU:
    return "discrete GPU";
  case WGPUAdapterType_IntegratedGPU:
    return "integrated GPU";
  case WGPUAdapterType_CPU:
    return "CPU/software";
  default:
    return "unknown";
  }
}

static int check_version(void) {
  /* This calls Rust but does not create an instance or load a GPU backend. */
  uint32_t version = wgpuGetVersion();
  printf("wgpu-native %" PRIu32 ".%" PRIu32 ".%" PRIu32 ".%" PRIu32
         " (0x%08" PRIx32 ")\n",
         version >> 24, (version >> 16) & 255, (version >> 8) & 255,
         version & 255, version);
  if (version != EXPECTED_VERSION) {
    fprintf(stderr,
            "version check failed: expected 29.0.1.1 (0x1d000101); build Rust "
            "with WGPU_NATIVE_VERSION=v29.0.1.1\n");
    return CHECK_VERSION;
  }
  puts("C -> Rust version check: PASS");
  return CHECK_OK;
}

#define REQUIRE(condition, code, message)                                      \
  do {                                                                        \
    if (!(condition)) {                                                       \
      fprintf(stderr, "compute check failed: %s\n", message);                  \
      result = code;                                                          \
      goto cleanup;                                                           \
    }                                                                         \
  } while (0)

#define REQUIRE_OBJECT(object, message)                                        \
  REQUIRE((object) &&                                                          \
              !atomic_load_explicit(&execution_error, memory_order_acquire),   \
          CHECK_EXECUTION, message)

static int check_compute(void) {
  const uint32_t input[] = {1, 2, 3, 4};
  const uint32_t expected[] = {0, 1, 7, 2};
  uint32_t output[4] = {0};
  struct AdapterRequest adapter_request = {0};
  struct DeviceRequest device_request = {0};
  struct MapRequest map_request = {0};
  WGPUInstance instance = NULL;
  WGPUAdapter adapter = NULL;
  WGPUDevice device = NULL;
  WGPUQueue queue = NULL;
  WGPUShaderModule module = NULL;
  WGPUBuffer storage = NULL, staging = NULL;
  WGPUComputePipeline pipeline = NULL;
  WGPUBindGroupLayout layout = NULL;
  WGPUBindGroup group = NULL;
  WGPUCommandEncoder encoder = NULL;
  WGPUComputePassEncoder pass = NULL;
  WGPUCommandBuffer commands = NULL;
  bool mapped = false;
  int result = CHECK_OK;

  /*
   * Avoid driver debug/validation callbacks in this experimental ABI port.
   * These are Vulkan-specific flags, not WebGPU's normal API validation.
   */
  WGPUInstanceExtras extras = {
      .chain = {.sType = (WGPUSType)WGPUSType_InstanceExtras},
      .backends = WGPUInstanceBackend_Vulkan,
      .flags = 0,
  };
  WGPUInstanceDescriptor instance_desc = WGPU_INSTANCE_DESCRIPTOR_INIT;
  instance_desc.nextInChain = &extras.chain;
  WGPURequestAdapterOptions adapter_options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
  adapter_options.backendType = WGPUBackendType_Vulkan;
  WGPUDeviceDescriptor device_desc = WGPU_DEVICE_DESCRIPTOR_INIT;
  device_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
  device_desc.deviceLostCallbackInfo.callback = device_lost;
  device_desc.uncapturedErrorCallbackInfo.callback = uncaptured_error;

  wgpuSetLogCallback(log_message, NULL);
  wgpuSetLogLevel(trace_enabled ? WGPULogLevel_Debug : WGPULogLevel_Warn);
  trace_stage("create instance");
  instance = wgpuCreateInstance(&instance_desc);
  REQUIRE(instance, CHECK_INITIALIZATION, "could not create a Vulkan instance");
  trace_stage("request adapter");
  wgpuInstanceRequestAdapter(
      instance, &adapter_options,
      (WGPURequestAdapterCallbackInfo){
          .mode = WGPUCallbackMode_AllowProcessEvents,
          .callback = adapter_ready,
          .userdata1 = &adapter_request,
      });
  result = wait_callback(instance, NULL, &adapter_request.done, "request adapter");
  adapter = adapter_request.adapter;
  if (result)
    goto cleanup;
  REQUIRE(adapter_request.status == WGPURequestAdapterStatus_Success && adapter,
          CHECK_INITIALIZATION, "no suitable Vulkan adapter");

  WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
  WGPUStatus info_status = wgpuAdapterGetInfo(adapter, &info);
  REQUIRE(info_status == WGPUStatus_Success, CHECK_INITIALIZATION,
          "could not read adapter information");
  fputs("adapter: ", stdout);
  print_view(stdout, info.device);
  fputs("; ", stdout);
  print_view(stdout, info.description);
  printf("; backend=%u (Vulkan=%u); type=%s\n", (unsigned)info.backendType,
         (unsigned)WGPUBackendType_Vulkan, adapter_kind(info.adapterType));
  bool is_vulkan = info.backendType == WGPUBackendType_Vulkan;
  trace_stage("release adapter information");
  wgpuAdapterInfoFreeMembers(info);
  REQUIRE(is_vulkan, CHECK_INITIALIZATION, "adapter did not use Vulkan");

  trace_stage("request device");
  wgpuAdapterRequestDevice(
      adapter, &device_desc,
      (WGPURequestDeviceCallbackInfo){
          .mode = WGPUCallbackMode_AllowProcessEvents,
          .callback = device_ready,
          .userdata1 = &device_request,
      });
  trace_stage("wait for device callback");
  result = wait_callback(instance, NULL, &device_request.done, "request device");
  device = device_request.device;
  if (result)
    goto cleanup;
  REQUIRE(device_request.status == WGPURequestDeviceStatus_Success && device,
          CHECK_INITIALIZATION, "could not create a WebGPU device");
  trace_stage("get device queue");
  queue = wgpuDeviceGetQueue(device);
  REQUIRE_OBJECT(queue, "could not obtain queue");

  WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
  source.code = (WGPUStringView){shader, sizeof(shader) - 1};
  WGPUShaderModuleDescriptor shader_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
  shader_desc.nextInChain = &source.chain;
  trace_stage("create WGSL shader module");
  module = wgpuDeviceCreateShaderModule(device, &shader_desc);
  REQUIRE_OBJECT(module, "embedded WGSL compilation failed");

  WGPUBufferDescriptor buffer_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
  buffer_desc.size = sizeof(input);
  buffer_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst |
                      WGPUBufferUsage_CopySrc;
  trace_stage("create storage buffer");
  storage = wgpuDeviceCreateBuffer(device, &buffer_desc);
  REQUIRE_OBJECT(storage, "could not create storage buffer");
  buffer_desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
  trace_stage("create readback buffer");
  staging = wgpuDeviceCreateBuffer(device, &buffer_desc);
  REQUIRE_OBJECT(staging, "could not create readback buffer");

  WGPUComputePipelineDescriptor pipeline_desc =
      WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
  pipeline_desc.compute.module = module;
  pipeline_desc.compute.entryPoint = (WGPUStringView){"main", WGPU_STRLEN};
  trace_stage("create compute pipeline");
  pipeline = wgpuDeviceCreateComputePipeline(device, &pipeline_desc);
  REQUIRE_OBJECT(pipeline, "could not create compute pipeline");
  trace_stage("create resource bindings");
  layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
  REQUIRE_OBJECT(layout, "could not obtain bind group layout");
  WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
  entry.binding = 0;
  entry.buffer = storage;
  entry.size = sizeof(input);
  WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
  group_desc.layout = layout;
  group_desc.entryCount = 1;
  group_desc.entries = &entry;
  group = wgpuDeviceCreateBindGroup(device, &group_desc);
  REQUIRE_OBJECT(group, "could not create bind group");

  trace_stage("encode compute and readback commands");
  encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
  REQUIRE_OBJECT(encoder, "could not create command encoder");
  pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
  REQUIRE_OBJECT(pass, "could not begin compute pass");
  wgpuComputePassEncoderSetPipeline(pass, pipeline);
  wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
  wgpuComputePassEncoderDispatchWorkgroups(pass, 4, 1, 1);
  wgpuComputePassEncoderEnd(pass);
  wgpuComputePassEncoderRelease(pass);
  pass = NULL;
  wgpuCommandEncoderCopyBufferToBuffer(encoder, storage, 0, staging, 0,
                                       sizeof(input));
  commands = wgpuCommandEncoderFinish(encoder, NULL);
  REQUIRE_OBJECT(commands, "could not finish command buffer");
  trace_stage("upload input");
  wgpuQueueWriteBuffer(queue, storage, 0, input, sizeof(input));
  trace_stage("submit compute commands");
  wgpuQueueSubmit(queue, 1, &commands);
  REQUIRE(!atomic_load_explicit(&execution_error, memory_order_acquire),
          CHECK_EXECUTION, "command submission failed");

  trace_stage("request readback mapping");
  wgpuBufferMapAsync(
      staging, WGPUMapMode_Read, 0, sizeof(output),
      (WGPUBufferMapCallbackInfo){
          .mode = WGPUCallbackMode_AllowProcessEvents,
          .callback = buffer_mapped,
          .userdata1 = &map_request,
      });
  trace_stage("wait for readback mapping");
  result = wait_callback(instance, device, &map_request.done, "map readback");
  if (result)
    goto cleanup;
  REQUIRE(map_request.status == WGPUMapAsyncStatus_Success, CHECK_EXECUTION,
          "mapping the readback buffer failed");
  mapped = true;
  trace_stage("verify mapped output");
  const void *data = wgpuBufferGetConstMappedRange(staging, 0, sizeof(output));
  REQUIRE_OBJECT(data, "mapped range was unavailable");
  memcpy(output, data, sizeof(output));
  printf("input: [1, 2, 3, 4]\nreadback: [%" PRIu32 ", %" PRIu32 ", %" PRIu32
         ", %" PRIu32 "]\n",
         output[0], output[1], output[2], output[3]);
  for (size_t i = 0; i < sizeof(output) / sizeof(output[0]); ++i) {
    if (output[i] != expected[i]) {
      fprintf(stderr, "element %zu: expected %" PRIu32 ", got %" PRIu32 "\n",
              i, expected[i], output[i]);
      result = CHECK_READBACK;
      goto cleanup;
    }
  }

cleanup:
  trace_stage("release resources");
  if (mapped)
    wgpuBufferUnmap(staging);
  if (commands)
    wgpuCommandBufferRelease(commands);
  if (pass)
    wgpuComputePassEncoderRelease(pass);
  if (encoder)
    wgpuCommandEncoderRelease(encoder);
  if (group)
    wgpuBindGroupRelease(group);
  if (layout)
    wgpuBindGroupLayoutRelease(layout);
  if (pipeline)
    wgpuComputePipelineRelease(pipeline);
  if (staging)
    wgpuBufferRelease(staging);
  if (storage)
    wgpuBufferRelease(storage);
  if (module)
    wgpuShaderModuleRelease(module);
  if (queue)
    wgpuQueueRelease(queue);
  if (device) {
    wgpuDeviceDestroy(device);
    wgpuDeviceRelease(device);
  }
  if (adapter)
    wgpuAdapterRelease(adapter);
  if (instance)
    wgpuInstanceRelease(instance);
  if (result == CHECK_OK &&
      atomic_load_explicit(&execution_error, memory_order_acquire))
    result = CHECK_EXECUTION;
  wgpuSetLogCallback(NULL, NULL);
  if (result == CHECK_OK)
    puts("headless Vulkan compute and readback: PASS");
  return result;
}

int main(int argc, char **argv) {
  ShowCrashReports();
  const char *trace = getenv("COSMO_WGPU_TRACE");
  trace_enabled = trace && *trace && strcmp(trace, "0");
  if (argc == 2 && !strcmp(argv[1], "--version"))
    return check_version();
  if (argc == 2 && !strcmp(argv[1], "--help")) {
    printf("Usage: %s [--version | --help]\n", argv[0]);
    puts("Default: check embedded WGSL compute through the Vulkan backend.\n"
         "--version: check the linked Rust API without loading a GPU backend.");
    return CHECK_OK;
  }
  if (argc != 1) {
    fprintf(stderr, "Usage: %s [--version | --help]\n", argv[0]);
    return CHECK_USAGE;
  }
  int result = check_version();
  return result ? result : check_compute();
}
