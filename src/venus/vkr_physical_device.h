/*
 * Copyright 2020 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VKR_PHYSICAL_DEVICE_H
#define VKR_PHYSICAL_DEVICE_H

#include "vkr_common.h"

#include "vn_protocol_renderer_util.h"

struct vkr_physical_device {
   struct vkr_object base;

   struct vn_physical_device_proc_table proc_table;

   VkPhysicalDeviceProperties properties;
   uint32_t api_version;

   VkExtensionProperties *extensions;
   uint32_t extension_count;

   bool KHR_external_memory_fd;
   bool EXT_external_memory_dma_buf;
   bool KHR_portability_subset;

   bool KHR_external_fence_fd;
   bool KHR_external_semaphore_fd;

   bool EXT_external_memory_host;

   /* the host cannot export memory as an fd, so memory the guest may map is
    * POSIX shm imported as host memory
    */
   bool use_host_memory_import;
   VkDeviceSize min_imported_host_pointer_alignment;

   VkPhysicalDeviceMemoryProperties memory_properties;
   bool mem_type_spoof;
   /* the driver's own flags of each memory type, before the spoof */
   VkMemoryPropertyFlags driver_mem_type_flags[VK_MAX_MEMORY_TYPES];
   VkPhysicalDeviceIDProperties id_properties;
   bool is_dma_buf_fd_export_supported;
   bool is_opaque_fd_export_supported;
   void *gbm_device;
   int udmabuf_dev_fd;

   VkQueueFamilyProperties *queue_family_properties;
   uint32_t queue_family_property_count;

   struct list_head devices;
};
VKR_DEFINE_OBJECT_CAST(physical_device, VK_OBJECT_TYPE_PHYSICAL_DEVICE, VkPhysicalDevice)

void
vkr_context_init_physical_device_dispatch(struct vkr_context *ctx);

void
vkr_physical_device_destroy(struct vkr_context *ctx,
                            struct vkr_physical_device *physical_dev);

static inline void
vkr_physical_device_mem_type_spoof(const struct vkr_physical_device *physical_dev,
                                   uint32_t *mem_type_bits)
{
   if (physical_dev->mem_type_spoof)
      *mem_type_bits = (*mem_type_bits << 1) | 1u;
}

#endif /* VKR_PHYSICAL_DEVICE_H */
