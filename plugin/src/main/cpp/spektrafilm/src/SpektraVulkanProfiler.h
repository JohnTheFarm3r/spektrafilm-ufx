#pragma once

#include <vulkan/vulkan.h>
#include <limits>
#include <utility>
#include <vector>

#include "SpektraRenderer.h"

namespace spektrafilm {

struct VulkanTimestampStorage {
  VkQueryPool pool = VK_NULL_HANDLE;
  uint32_t capacity = 0;
};

class VulkanPassProfiler {
public:
  VulkanPassProfiler(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t queueFamily,
                     bool enabled, uint32_t passCapacity, VulkanTimestampStorage &storage)
    : device_(device), storage_(storage) {
    if (!enabled) return;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, queues.data());
    if (queueFamily >= count || queues[queueFamily].timestampValidBits == 0 ||
        !properties.limits.timestampComputeAndGraphics) return;
    validBits_ = queues[queueFamily].timestampValidBits;
    period_ = properties.limits.timestampPeriod;
    const uint32_t required = passCapacity * 2u;
    if (storage_.capacity < required) {
      if (storage_.pool != VK_NULL_HANDLE) vkDestroyQueryPool(device_, storage_.pool, nullptr);
      storage_ = {};
      VkQueryPoolCreateInfo info{};
      info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
      info.queryType = VK_QUERY_TYPE_TIMESTAMP;
      info.queryCount = required;
      if (vkCreateQueryPool(device_, &info, nullptr, &storage_.pool) != VK_SUCCESS) return;
      storage_.capacity = required;
    }
    active_ = storage_.pool != VK_NULL_HANDLE;
  }

  bool active() const { return active_; }

  void reset(VkCommandBuffer commandBuffer) {
    if (active_) vkCmdResetQueryPool(commandBuffer, storage_.pool, 0, storage_.capacity);
  }

  void dispatch(VkCommandBuffer commandBuffer, const char *name, uint32_t operation,
                uint32_t x, uint32_t y, uint32_t z, uint32_t localX = 32, uint32_t localY = 8) {
    const uint32_t query = static_cast<uint32_t>(passes_.size()) * 2u;
    const bool timed = active_ && query + 1u < storage_.capacity;
    if (timed) vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, storage_.pool, query);
    vkCmdDispatch(commandBuffer, x, y, z);
    if (timed) {
      vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, storage_.pool, query + 1u);
      RendererPassDiagnostics pass{};
      pass.name = name;
      if (operation != UINT32_MAX) pass.name += "/" + std::to_string(operation);
      pass.width = x * localX;
      pass.height = y * localY;
      pass.depth = z;
      pass.threadgroupWidth = localX;
      pass.threadgroupHeight = localY;
      passes_.push_back(std::move(pass));
    } else if (active_) {
      overflow_ = true;
    }
  }

  // Call only after the submission fence has completed.
  void collect(RendererDiagnostics &diagnostics) {
    if (!active_ || passes_.empty() || overflow_) return;
    std::vector<uint64_t> timestamps(passes_.size() * 2u);
    if (vkGetQueryPoolResults(device_, storage_.pool, 0, static_cast<uint32_t>(timestamps.size()),
                             timestamps.size() * sizeof(uint64_t), timestamps.data(), sizeof(uint64_t),
                             VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
    const uint64_t mask = validBits_ == 64u ? std::numeric_limits<uint64_t>::max()
                                           : (uint64_t{1} << validBits_) - 1u;
    diagnostics.gpuCommandBufferMs = 0.0;
    for (size_t i = 0; i < passes_.size(); ++i) {
      passes_[i].gpuMs = static_cast<double>((timestamps[i * 2u + 1u] - timestamps[i * 2u]) & mask)
                        * period_ / 1e6;
      passes_[i].gpuTimeAvailable = true;
      diagnostics.gpuCommandBufferMs += passes_[i].gpuMs;
    }
    diagnostics.passes = std::move(passes_);
    diagnostics.passCount = static_cast<uint32_t>(diagnostics.passes.size());
    diagnostics.passGpuTimingAvailable = true;
    diagnostics.passTimingMode = "vulkan-timestamps";
  }

private:
  VkDevice device_;
  VulkanTimestampStorage &storage_;
  uint32_t validBits_ = 0;
  double period_ = 0.0;
  bool active_ = false;
  bool overflow_ = false;
  std::vector<RendererPassDiagnostics> passes_;
};

} // namespace spektrafilm
