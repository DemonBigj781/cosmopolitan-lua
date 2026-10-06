/* SPDX-License-Identifier: MIT
 * Minimal signatures for loader isolation tests, not production dispatch.
 * Production uses metadata generated from the pinned Ash tables. */
static const struct VulkanSignature kVulkanSignatures[] = {
  {"vkCreateDevice", "iiii"},
  {"vkGetDeviceProcAddr", "ii"},
  {"vkGetInstanceProcAddr", "ii"},
};
