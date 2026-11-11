/*
 * Copyright © 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "tu_device.h"
#include "tu_sampler.h"

static uint32_t
tu_embedded_sampler_hash(const void *_s)
{
   const struct tu_embedded_sampler *s =
      (const struct tu_embedded_sampler *)_s;
   return _mesa_hash_data(&s->descriptor, sizeof(s->descriptor));
}

static bool
tu_embedded_sampler_equals(const void *_a, const void *_b)
{
   const struct tu_embedded_sampler *a =
      (const struct tu_embedded_sampler *)_a;
   const struct tu_embedded_sampler *b =
      (const struct tu_embedded_sampler *)_b;
   return !memcmp(&a->descriptor, &b->descriptor, sizeof(a->descriptor));
}

VkResult
tu_embedded_sampler_heap_init(struct tu_device *dev)
{
   VkResult result = tu_bo_init_new(
      dev, NULL, &dev->embedded_sampler_bo, 
      TU_MAX_EMBEDDED_SAMPLERS * A6XX_TEX_SAMP_DWORDS * sizeof(uint32_t),
      (enum tu_bo_alloc_flags) (TU_BO_ALLOC_ALLOW_DUMP |
                                TU_BO_ALLOC_INTERNAL_RESOURCE |
                                TU_BO_ALLOC_GPU_READ_ONLY),
      "embedded sampler heap");

   if (result != VK_SUCCESS)
      return result;

   result = tu_bo_map(dev, dev->embedded_sampler_bo, NULL);
   if (result != VK_SUCCESS) {
      tu_bo_finish(dev, dev->embedded_sampler_bo);
      return result;
   }

   dev->embedded_sampler_ht =
      _mesa_set_create(NULL, tu_embedded_sampler_hash,
                       tu_embedded_sampler_equals);
   list_inithead(&dev->free_embedded_samplers);
   mtx_init(&dev->embedded_sampler_mutex, mtx_plain);

   return VK_SUCCESS;
}

void
tu_embedded_sampler_heap_finish(struct tu_device *dev)
{
   mtx_destroy(&dev->embedded_sampler_mutex);

   assert(dev->embedded_sampler_ht->entries == 0);
   _mesa_set_destroy(dev->embedded_sampler_ht, NULL);

   list_for_each_entry_safe (struct tu_embedded_sampler, samp,
                             &dev->free_embedded_samplers, free_link) {
      vk_free(&dev->vk.alloc, samp);
   }

   tu_bo_finish(dev, dev->embedded_sampler_bo);
}

static VkResult
tu_embedded_sampler_heap_get_locked(struct tu_device *dev,
                                    const uint32_t *descriptor,
                                    struct tu_embedded_sampler **samp_out)
{
   struct tu_embedded_sampler key;
   memcpy(key.descriptor, descriptor, sizeof(key.descriptor));

   struct set_entry *entry =
      _mesa_set_search(dev->embedded_sampler_ht, &key);
   if (entry != NULL) {
      *samp_out =
         tu_embedded_sampler_ref((struct tu_embedded_sampler *)entry->key);
      return VK_SUCCESS;
   }

   struct tu_embedded_sampler *samp;
   if (!list_is_empty(&dev->free_embedded_samplers)) {
      samp = list_first_entry(&dev->free_embedded_samplers,
                              struct tu_embedded_sampler, free_link);
      assert(samp->refcnt == 0);
      list_del(&samp->free_link);
   } else {
      samp = (struct tu_embedded_sampler *)
         vk_zalloc(&dev->vk.alloc, sizeof(struct tu_embedded_sampler),
                   alignof(struct tu_embedded_sampler),
                   VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (samp == NULL)
         return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
      samp->index = dev->embedded_sampler_count++;
      assert(samp->index < TU_MAX_EMBEDDED_SAMPLERS);
   }

   memcpy(samp->descriptor, descriptor, sizeof(samp->descriptor));
   char *map = (char *)dev->embedded_sampler_bo->map +
      samp->index * sizeof(samp->descriptor);
   memcpy(map, descriptor, sizeof(samp->descriptor));

   *samp_out = tu_embedded_sampler_ref(samp);
   return VK_SUCCESS;
}

VkResult
tu_embedded_sampler_heap_get(struct tu_device *dev,
                             const uint32_t *descriptor,
                             struct tu_embedded_sampler **samp_out)
{
   VkResult result;

   mtx_lock(&dev->embedded_sampler_mutex);
   result = tu_embedded_sampler_heap_get_locked(dev, descriptor, samp_out);
   mtx_unlock(&dev->embedded_sampler_mutex);

   return result;
}

static void
tu_embedded_sampler_heap_put_locked(struct tu_device *dev,
                                    struct tu_embedded_sampler *samp)
{
   if (!p_atomic_dec_zero(&samp->refcnt))
      return;

   _mesa_set_remove_key(dev->embedded_sampler_ht, samp);

   memset(samp->descriptor, 0, sizeof(samp->descriptor));
   char *map = (char *)dev->embedded_sampler_bo->map +
      samp->index * sizeof(samp->descriptor);
   memset(map, 0, sizeof(samp->descriptor));
   list_addtail(&samp->free_link, &dev->free_embedded_samplers);
}

void
tu_embedded_sampler_heap_put(struct tu_device *dev,
                             struct tu_embedded_sampler *samp)
{
   if (p_atomic_dec_not_one(&samp->refcnt))
      return;

   mtx_lock(&dev->embedded_sampler_mutex);
   tu_embedded_sampler_heap_put_locked(dev, samp);
   mtx_unlock(&dev->embedded_sampler_mutex);
}
