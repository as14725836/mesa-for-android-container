/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_sampler.h"

#include "tu_device.h"
#include "tu_util.h"

template <chip CHIP>
void
tu_get_sampler_descriptor(struct tu_device *device,
                          const struct vk_sampler_state *sampler,
                          uint32_t custom_border_color_index,
                          uint32_t *descriptor)
{
   bool fast_border_color_enable = false;
   enum a6xx_fast_border_color fast_border_color = A6XX_BORDER_COLOR_0_0_0_0;

   /* The first border colors in the table correspond to the standard Vulkan
    * border colors.
    */
   uint32_t border_color = (unsigned) sampler->border_color;
   if (vk_border_color_is_custom(sampler->border_color)) {
      border_color = 2 * custom_border_color_index +
         (unsigned)(sampler->border_color == VK_BORDER_COLOR_INT_CUSTOM_EXT) +
                    TU_BORDER_COLOR_BUILTIN;
   } else if (sampler->format != VK_FORMAT_UNDEFINED ||
        device->instance->drirc.misc.enable_fast_border_color_for_undefined_formats) {
      fast_border_color_enable = true;
      switch (sampler->border_color) {
         case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
         case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
            fast_border_color = A6XX_BORDER_COLOR_0_0_0_0;
            break;
         case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
         case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
            fast_border_color = A6XX_BORDER_COLOR_0_0_0_1;
            break;
         case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
         case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
            fast_border_color = A6XX_BORDER_COLOR_1_1_1_1;
            break;
         default:
            UNREACHABLE("unknown border color");
      }
   }

   unsigned aniso = sampler->anisotropy_enable ?
      util_last_bit(MIN2(static_cast<uint32_t>(sampler->max_anisotropy) >> 1, 8)) : 0;
   bool miplinear = (sampler->mipmap_mode == VK_SAMPLER_MIPMAP_MODE_LINEAR);
   float min_lod = CLAMP(sampler->min_lod, 0.0f, 4095.0f / 256.0f);
   float max_lod = CLAMP(sampler->max_lod, 0.0f, 4095.0f / 256.0f);

   if (CHIP >= A8XX) {
      descriptor[0] =
         COND(miplinear, A8XX_TEX_SAMP_0_MIPFILTER_LINEAR_NEAR) |
         A8XX_TEX_SAMP_0_XY_MAG(tu6_tex_filter(sampler->mag_filter, aniso)) |
         A8XX_TEX_SAMP_0_XY_MIN(tu6_tex_filter(sampler->min_filter, aniso)) |
         A8XX_TEX_SAMP_0_WRAP_S(tu6_tex_wrap(sampler->address_mode_u)) |
         A8XX_TEX_SAMP_0_WRAP_T(tu6_tex_wrap(sampler->address_mode_v)) |
         A8XX_TEX_SAMP_0_WRAP_R(tu6_tex_wrap(sampler->address_mode_w)) |
         A8XX_TEX_SAMP_0_LOD_BIAS(sampler->mip_lod_bias) |
         A8XX_TEX_SAMP_0_ANISO(static_cast<enum a6xx_tex_aniso>(aniso));

      descriptor[1] =
         A8XX_TEX_SAMP_1_MAX_LOD(max_lod) |
         A8XX_TEX_SAMP_1_MIN_LOD(min_lod) |
         COND(sampler->compare_enable, A8XX_TEX_SAMP_1_COMPARE_FUNC(tu6_compare_func(sampler->compare_op))) |
         COND(sampler->flags & VK_SAMPLER_CREATE_NON_SEAMLESS_CUBE_MAP_BIT_EXT,
            A8XX_TEX_SAMP_1_CUBEMAPSEAMLESSFILTOFF) |
         COND(sampler->unnormalized_coordinates, A8XX_TEX_SAMP_1_UNNORM_COORDS);
      descriptor[2] =
         A8XX_TEX_SAMP_2_BCOLOR(border_color) |
         A8XX_TEX_SAMP_2_FASTBORDERCOLOR(fast_border_color) |
         COND(fast_border_color_enable, A8XX_TEX_SAMP_2_FASTBORDERCOLOREN);
      descriptor[3] = 0;

      if (sampler->reduction_mode != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE) {
         descriptor[1] |= A8XX_TEX_SAMP_1_REDUCTION_MODE(
            tu6_reduction_mode(sampler->reduction_mode));
      }

      if (sampler->has_ycbcr_conversion &&
          sampler->ycbcr_conversion.chroma_filter == VK_FILTER_LINEAR) {
         descriptor[1] |= A8XX_TEX_SAMP_1_CHROMA_LINEAR;
      }
   } else {
      descriptor[0] =
         COND(miplinear, A6XX_TEX_SAMP_0_MIPFILTER_LINEAR_NEAR) |
         A6XX_TEX_SAMP_0_XY_MAG(tu6_tex_filter(sampler->mag_filter, aniso)) |
         A6XX_TEX_SAMP_0_XY_MIN(tu6_tex_filter(sampler->min_filter, aniso)) |
         A6XX_TEX_SAMP_0_ANISO(static_cast<enum a6xx_tex_aniso>(aniso)) |
         A6XX_TEX_SAMP_0_WRAP_S(tu6_tex_wrap(sampler->address_mode_u)) |
         A6XX_TEX_SAMP_0_WRAP_T(tu6_tex_wrap(sampler->address_mode_v)) |
         A6XX_TEX_SAMP_0_WRAP_R(tu6_tex_wrap(sampler->address_mode_w)) |
         A6XX_TEX_SAMP_0_LOD_BIAS(sampler->mip_lod_bias);
      descriptor[1] =
         COND(sampler->flags & VK_SAMPLER_CREATE_NON_SEAMLESS_CUBE_MAP_BIT_EXT,
            A6XX_TEX_SAMP_1_CUBEMAPSEAMLESSFILTOFF) |
         COND(sampler->unnormalized_coordinates, A6XX_TEX_SAMP_1_UNNORM_COORDS) |
         A6XX_TEX_SAMP_1_MIN_LOD(min_lod) |
         A6XX_TEX_SAMP_1_MAX_LOD(max_lod) |
         COND(sampler->compare_enable,
            A6XX_TEX_SAMP_1_COMPARE_FUNC(tu6_compare_func(sampler->compare_op)));
      descriptor[2] =
         A6XX_TEX_SAMP_2_BCOLOR(border_color) |
         A6XX_TEX_SAMP_2_FASTBORDERCOLOR(fast_border_color) |
         COND(fast_border_color_enable, A6XX_TEX_SAMP_2_FASTBORDERCOLOREN);
      descriptor[3] = 0;

      if (sampler->reduction_mode != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE) {
         descriptor[2] |= A6XX_TEX_SAMP_2_REDUCTION_MODE(
            tu6_reduction_mode(sampler->reduction_mode));
      }

      if (sampler->has_ycbcr_conversion &&
          sampler->ycbcr_conversion.chroma_filter == VK_FILTER_LINEAR) {
         descriptor[2] |= A6XX_TEX_SAMP_2_CHROMA_LINEAR;
      }
   }

   /* TODO:
    * A6XX_TEX_SAMP_1_MIPFILTER_LINEAR_FAR disables mipmapping, but vk has no NONE mipfilter?
    */
}
TU_GENX(tu_get_sampler_descriptor);

static VkResult
tu_register_border_color(struct tu_device *device,
                         const VkClearColorValue *border_color,
                         VkFormat format,
                         bool replay,
                         uint32_t *border_color_index)
{
   mtx_lock(&device->mutex);
   if (replay) {
      if (*border_color_index >= TU_BORDER_COLOR_COUNT ||
          !BITSET_TEST(device->custom_border_color, *border_color_index)) {
         mtx_unlock(&device->mutex);
         return vk_error(device, VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS);
      }
   } else {
      *border_color_index = BITSET_FFS(device->custom_border_color) - 1;
      if (*border_color_index >= TU_BORDER_COLOR_COUNT) {
         mtx_unlock(&device->mutex);
         return vk_error(device, VK_ERROR_TOO_MANY_OBJECTS);
      }
   }
   BITSET_CLEAR(device->custom_border_color, *border_color_index);
   mtx_unlock(&device->mutex);

   for (unsigned i = 0; i < 2; i++) {
      bool is_int = i == 1;
      VkClearColorValue color = *border_color;

      if (format == VK_FORMAT_D24_UNORM_S8_UINT &&
          is_int && device->use_z24uint_s8uint) {
         /* When sampling stencil using the special Z24UINT_S8UINT format, the
          * border color is in the second component. Note: if
          * customBorderColorWithoutFormat is enabled, we may miss doing this
          * here if the format isn't specified, which is why we don't use that
          * format.
          */
         color.uint32[1] = color.uint32[0];
      }

      tu6_pack_border_color(
         &device->global_bo_map->bcolor[*border_color_index * 2 + i], &color,
         is_int);
   }

   return VK_SUCCESS;
}

static void
tu_unregister_border_color(struct tu_device *device, uint32_t index)
{
   mtx_lock(&device->mutex);
   BITSET_SET(device->custom_border_color, index);
   mtx_unlock(&device->mutex);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_RegisterCustomBorderColorEXT(
    VkDevice                                        _device,
    const VkSamplerCustomBorderColorCreateInfoEXT*  pBorderColor,
    VkBool32                                        requestIndex,
    uint32_t*                                       pIndex)
{
   VK_FROM_HANDLE(tu_device, device, _device);

   return tu_register_border_color(device, &pBorderColor->customBorderColor,
                                   pBorderColor->format, requestIndex,
                                   pIndex);
}

VKAPI_ATTR void VKAPI_CALL
tu_UnregisterCustomBorderColorEXT(
    VkDevice                                        _device,
    uint32_t                                        index)
{
   VK_FROM_HANDLE(tu_device, device, _device);

   tu_unregister_border_color(device, index);
}

template <chip CHIP>
VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateSampler(VkDevice _device,
                 const VkSamplerCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkSampler *pSampler)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   struct tu_sampler *sampler;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);

   sampler = static_cast<struct tu_sampler *>(vk_sampler_create(
      &device->vk, pCreateInfo, pAllocator, sizeof(*sampler)));
   if (!sampler)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   uint32_t custom_border_color_index = 0;
   if (vk_border_color_is_custom(pCreateInfo->borderColor)) {
      const VkSamplerCustomBorderColorIndexCreateInfoEXT *border_color_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              SAMPLER_CUSTOM_BORDER_COLOR_INDEX_CREATE_INFO_EXT);

      if (border_color_info) {
         custom_border_color_index = border_color_info->index;
         /* We purposefully don't store the index in the sampler so that it
          * isn't automatically unregistered. It's the user's responsibility
          * to unregister the index, since they registered it themselves. We
          * also don't check whether it's registered, since it's allowed to
          * register it later.
          */
      } else {
         bool replay = false;

         const VkOpaqueCaptureDescriptorDataCreateInfoEXT *opaque_info =
            vk_find_struct_const(pCreateInfo->pNext,
                                 OPAQUE_CAPTURE_DESCRIPTOR_DATA_CREATE_INFO_EXT);
         if (opaque_info && opaque_info->opaqueCaptureDescriptorData) {
            replay = true;
            custom_border_color_index =
               *static_cast<const uint32_t *>(opaque_info->opaqueCaptureDescriptorData);
         }

         VkResult result =
            tu_register_border_color(device, &sampler->vk.border_color_value,
                                     sampler->vk.format, replay,
                                     &custom_border_color_index);
         if (result != VK_SUCCESS) {
            vk_sampler_destroy(&device->vk, pAllocator, &sampler->vk);
            return result;
         }

         sampler->custom_border_color = true;
         sampler->border_color_index = custom_border_color_index;
      }
   }

   struct vk_sampler_state state;
   vk_sampler_state_init(&state, pCreateInfo);

   tu_get_sampler_descriptor<CHIP>(device, &state, custom_border_color_index,
                                   sampler->descriptor);

   const struct VkSamplerYcbcrConversionInfo *ycbcr_conversion =
      vk_find_struct_const(pCreateInfo->pNext,  SAMPLER_YCBCR_CONVERSION_INFO);

   sampler->vk.ycbcr_conversion = ycbcr_conversion ?
      vk_ycbcr_conversion_from_handle(ycbcr_conversion->conversion) : NULL;

   *pSampler = tu_sampler_to_handle(sampler);

   return VK_SUCCESS;
}
TU_GENX(tu_CreateSampler);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_DestroySampler(VkDevice _device,
                  VkSampler _sampler,
                  const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_sampler, sampler, _sampler);

   if (!sampler)
      return;

   if (sampler->custom_border_color) {
      /* if the sampler had a custom border color, free it. */
      tu_unregister_border_color(device, sampler->border_color_index);
   }

   vk_sampler_destroy(&device->vk, pAllocator, &sampler->vk);
}
TU_GENX(tu_DestroySampler);

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetSamplerOpaqueCaptureDescriptorDataEXT(
   VkDevice _device,
   const VkSamplerCaptureDescriptorDataInfoEXT *pInfo,
   void *pData)
{
   VK_FROM_HANDLE(tu_sampler, sampler, pInfo->sampler);

   *static_cast<uint32_t *>(pData) =
      sampler->custom_border_color ? sampler->border_color_index : 0;

   return VK_SUCCESS;
}
