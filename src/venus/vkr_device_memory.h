/*
 * Copyright 2020 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VKR_DEVICE_MEMORY_H
#define VKR_DEVICE_MEMORY_H

#include "vkr_common.h"

struct gbm_bo;
/* POSIX shm mapped into the context: backing imported as host memory for
 * memory the guest can map, or the map of a shm resource. A resource made
 * from such memory shares its map, so the pages stay mapped until the last
 * reference goes.
 */
struct vkr_shm {
   /* -1 when the shm is never exported */
   int fd;
   void *ptr;
   size_t size;
   atomic_uint refcount;
};

struct vkr_device_memory {
   struct vkr_object base;

   struct vkr_device *device;

   bool might_export;

   uint32_t property_flags;
   uint32_t valid_fd_types;

   /* gbm bo backing non-external mappable memory */
   struct gbm_bo *gbm_bo;

   /* udmabuf backing non-external mappable memory */
   int udmabuf_fd;

   /* shm imported as host memory when the host cannot export an fd: the
    * memory's own, or the map of the shm resource it was imported from
    */
   struct vkr_shm *shm;

   uint64_t allocation_size;
   uint32_t memory_type_index;

   bool exported;
};
VKR_DEFINE_OBJECT_CAST(device_memory, VK_OBJECT_TYPE_DEVICE_MEMORY, VkDeviceMemory)

void
vkr_context_init_device_memory_dispatch(struct vkr_context *ctx);

void
vkr_device_memory_release(struct vkr_device_memory *mem);

bool
vkr_device_memory_export_blob(struct vkr_device_memory *mem,
                              uint64_t blob_size,
                              uint32_t blob_flags,
                              struct virgl_context_blob *out_blob);

struct vkr_shm *
vkr_shm_map(int fd, size_t size);

struct vkr_shm *
vkr_shm_ref(struct vkr_shm *shm);

void
vkr_shm_unref(struct vkr_shm *shm);

#endif /* VKR_DEVICE_MEMORY_H */
