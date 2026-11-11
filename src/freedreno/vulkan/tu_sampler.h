/*
 * Copyright © 2014 Valentine Burley
 * SPDX-License-Identifier: MIT
 */

#ifndef TU_SAMPLER_H
#define TU_SAMPLER_H

#include "tu_common.h"

#include "vk_sampler.h"
#include "vk_ycbcr_conversion.h"

struct tu_sampler {
   struct vk_sampler vk;

   uint32_t descriptor[A6XX_TEX_SAMP_DWORDS];

   bool custom_border_color;
   uint32_t border_color_index;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(tu_sampler, vk.base, VkSampler,
                               VK_OBJECT_TYPE_SAMPLER)

template <chip CHIP>
void
tu_get_sampler_descriptor(struct tu_device *device,
                          const struct vk_sampler_state *sampler,
                          uint32_t custom_border_color_index,
                          uint32_t *descriptor);

struct tu_embedded_sampler {
   uint32_t refcnt;

   /** Index in the embedded sampler heap */
   uint32_t index;

   union {
      uint32_t descriptor[A6XX_TEX_SAMP_DWORDS];

      /** Link in free_embedded_samplers */
      struct list_head free_link;
   };
};

static inline struct tu_embedded_sampler *
tu_embedded_sampler_ref(struct tu_embedded_sampler *samp)
{
   p_atomic_inc(&samp->refcnt);
   return samp;
}

VkResult tu_embedded_sampler_heap_init(struct tu_device *dev);

void tu_embedded_sampler_heap_finish(struct tu_device *dev);

VkResult tu_embedded_sampler_heap_get(struct tu_device *dev,
                                      const uint32_t *descriptor,
                                      struct tu_embedded_sampler **samp_out);

void tu_embedded_sampler_heap_put(struct tu_device *dev,
                                  struct tu_embedded_sampler *samp);

#endif /* TU_SAMPLER_H */
