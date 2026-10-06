/* SPDX-License-Identifier: MIT */
/* Direct static-driver diagnostic. Shader validation is in wgpu_native's
   Collatz and f32 workgroup-matrix tests. This does not load libvulkan. */
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
lvp_GetInstanceProcAddr(VkInstance instance, const char *name);

int main(void) {
  PFN_vkCreateInstance create_instance = (PFN_vkCreateInstance)
      lvp_GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
  if (!create_instance) {
    fputs("Missing static vkCreateInstance\n", stderr);
    return 1;
  }
  VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "Cosmopolitan static lavapipe probe",
      .apiVersion = VK_API_VERSION_1_1,
  };
  VkInstanceCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app,
  };
  VkInstance instance = VK_NULL_HANDLE;
  VkResult result = create_instance(&info, NULL, &instance);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "Static vkCreateInstance failed: %d\n", result);
    return 2;
  }
  PFN_vkDestroyInstance destroy_instance = (PFN_vkDestroyInstance)
      lvp_GetInstanceProcAddr(instance, "vkDestroyInstance");
  PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)
      lvp_GetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
  PFN_vkGetPhysicalDeviceProperties properties =
      (PFN_vkGetPhysicalDeviceProperties)lvp_GetInstanceProcAddr(
          instance, "vkGetPhysicalDeviceProperties");
  if (!destroy_instance || !enumerate || !properties) {
    fputs("Missing static instance dispatch entry\n", stderr);
    if (destroy_instance) destroy_instance(instance, NULL);
    return 3;
  }
  uint32_t count = 0;
  result = enumerate(instance, &count, NULL);
  if (result != VK_SUCCESS || count == 0) {
    fprintf(stderr, "Static device enumeration failed: %d (%u devices)\n",
            result, count);
    destroy_instance(instance, NULL);
    return 4;
  }
  VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
  if (!devices) {
    destroy_instance(instance, NULL);
    return 5;
  }
  result = enumerate(instance, &count, devices);
  int status = 0;
  if (result != VK_SUCCESS || count == 0) {
    fprintf(stderr, "Static device readback failed: %d\n", result);
    status = 6;
  } else {
    VkPhysicalDeviceProperties props;
    properties(devices[0], &props);
    printf("Static Mesa device: %s\n", props.deviceName);
    printf("Vulkan API: %u.%u.%u; device type: %u\n",
           VK_API_VERSION_MAJOR(props.apiVersion),
           VK_API_VERSION_MINOR(props.apiVersion),
           VK_API_VERSION_PATCH(props.apiVersion), props.deviceType);
    if (props.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) {
      fputs("Embedded driver did not report a CPU device\n", stderr);
      status = 7;
    }
  }
  free(devices);
  destroy_instance(instance, NULL);
  if (!status) puts("PASS: direct static lavapipe instance and CPU enumeration");
  return status;
}
