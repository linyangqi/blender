/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2011-2022 Blender Foundation */

#pragma once

// clang-format off
#include "kernel/closure/bsdf_ashikhmin_velvet.h"
#include "kernel/closure/bsdf_diffuse.h"
#include "kernel/closure/bsdf_oren_nayar.h"
#include "kernel/closure/bsdf_phong_ramp.h"
#include "kernel/closure/bsdf_diffuse_ramp.h"
#include "kernel/closure/bsdf_microfacet.h"
#include "kernel/closure/bsdf_microfacet_multi.h"
#include "kernel/closure/bsdf_reflection.h"
#include "kernel/closure/bsdf_refraction.h"
#include "kernel/closure/bsdf_transparent.h"
#include "kernel/closure/bsdf_ashikhmin_shirley.h"
#include "kernel/closure/bsdf_toon.h"
#include "kernel/closure/bsdf_hair.h"
#include "kernel/closure/bsdf_hair_principled.h"
#include "kernel/closure/bsdf_principled_diffuse.h"
#include "kernel/closure/bsdf_principled_sheen.h"
#include "kernel/closure/bssrdf.h"
#include "kernel/closure/volume.h"
// clang-format on

CCL_NAMESPACE_BEGIN

/* Returns the square of the roughness of the closure if it has roughness,
 * 0 for singular closures and 1 otherwise. */
ccl_device_inline float bsdf_get_specular_roughness_squared(ccl_private const ShaderClosure *sc)
{
  if (CLOSURE_IS_BSDF_SINGULAR(sc->type)) {
    return 0.0f;
  }

  if (CLOSURE_IS_BSDF_MICROFACET(sc->type)) {
    ccl_private MicrofacetBsdf *bsdf = (ccl_private MicrofacetBsdf *)sc;
    return bsdf->alpha_x * bsdf->alpha_y;
  }

  return 1.0f;
}

ccl_device_inline float bsdf_get_roughness_squared(ccl_private const ShaderClosure *sc)
{
  /* This version includes diffuse, mainly for baking Principled BSDF
   * where specular and metallic zero otherwise does not bake the
   * specified roughness parameter. */
  if (sc->type == CLOSURE_BSDF_OREN_NAYAR_ID) {
    ccl_private OrenNayarBsdf *bsdf = (ccl_private OrenNayarBsdf *)sc;
    return sqr(sqr(bsdf->roughness));
  }

  if (sc->type == CLOSURE_BSDF_PRINCIPLED_DIFFUSE_ID) {
    ccl_private PrincipledDiffuseBsdf *bsdf = (ccl_private PrincipledDiffuseBsdf *)sc;
    return sqr(sqr(bsdf->roughness));
  }

  if (CLOSURE_IS_BSDF_DIFFUSE(sc->type)) {
    return 0.0f;
  }

  return bsdf_get_specular_roughness_squared(sc);
}

/* An additional term to smooth illumination on grazing angles when using bump mapping.
 * Based on "Taming the Shadow Terminator" by Matt Jen-Yuan Chiang,
 * Yining Karl Li and Brent Burley. */
ccl_device_inline float bump_shadowing_term(float3 Ng, float3 N, float3 I)
{
  float g = safe_divide(dot(Ng, I), dot(N, I) * dot(Ng, N));

  /* If the incoming light is on the unshadowed side, return full brightness. */
  if (g >= 1.0f) {
    return 1.0f;
  }

  /* If the incoming light points away from the surface, return black. */
  if (g < 0.0f) {
    return 0.0f;
  }

  /* Return smoothed value to avoid discontinuity at perpendicular angle. */
  float g2 = sqr(g);
  return -g2 * g + g2 + g;
}

ccl_device_inline float shift_cos_in(float cos_in, const float frequency_multiplier)
{
  /* Shadow terminator workaround, taken from Appleseed.
   * SPDX-License-Identifier: MIT
   * Copyright (c) 2019 Francois Beaune, The appleseedhq Organization */
  cos_in = min(cos_in, 1.0f);

  const float angle = fast_acosf(cos_in);
  const float val = max(cosf(angle * frequency_multiplier), 0.0f) / cos_in;
  return val;
}

ccl_device_inline int bsdf_sample(KernelGlobals kg,
                                  ccl_private ShaderData *sd,
                                  ccl_private const ShaderClosure *sc,
                                  float randu,
                                  float randv,
                                  ccl_private float3 *eval,
                                  ccl_private float3 *omega_in,
                                  ccl_private differential3 *domega_in,
                                  ccl_private float *pdf)
{
  /* For curves use the smooth normal, particularly for ribbons the geometric
   * normal gives too much darkening otherwise. */
  int label;
  const float3 Ng = (sd->type & PRIMITIVE_CURVE) ? sc->N : sd->Ng;

  switch (sc->type) {
    case CLOSURE_BSDF_DIFFUSE_ID:
      label = bsdf_diffuse_sample(sc,
                                  Ng,
                                  sd->I,
                                  sd->dI.dx,
                                  sd->dI.dy,
                                  randu,
                                  randv,
                                  eval,
                                  omega_in,
                                  &domega_in->dx,
                                  &domega_in->dy,
                                  pdf);
      break;
#ifdef __SVM__
    case CLOSURE_BSDF_OREN_NAYAR_ID:
      label = bsdf_oren_nayar_sample(sc,
                                     Ng,
                                     sd->I,
                                     sd->dI.dx,
                                     sd->dI.dy,
                                     randu,
                                     randv,
                                     eval,
                                     omega_in,
                                     &domega_in->dx,
                                     &domega_in->dy,
                                     pdf);
      break;
#  ifdef __OSL__
    case CLOSURE_BSDF_PHONG_RAMP_ID:
      label = bsdf_phong_ramp_sample(sc,
                                     Ng,
                                     sd->I,
                                     sd->dI.dx,
                                     sd->dI.dy,
                                     randu,
                                     randv,
                                     eval,
                                     omega_in,
                                     &domega_in->dx,
                                     &domega_in->dy,
                                     pdf);
      break;
    case CLOSURE_BSDF_DIFFUSE_RAMP_ID:
      label = bsdf_diffuse_ramp_sample(sc,
                                       Ng,
                                       sd->I,
                                       sd->dI.dx,
                                       sd->dI.dy,
                                       randu,
                                       randv,
                                       eval,
                                       omega_in,
                                       &domega_in->dx,
                                       &domega_in->dy,
                                       pdf);
      break;
#  endif
    case CLOSURE_BSDF_TRANSLUCENT_ID:
      label = bsdf_translucent_sample(sc,
                                      Ng,
                                      sd->I,
                                      sd->dI.dx,
                                      sd->dI.dy,
                                      randu,
                                      randv,
                                      eval,
                                      omega_in,
                                      &domega_in->dx,
                                      &domega_in->dy,
                                      pdf);
      break;
    case CLOSURE_BSDF_REFLECTION_ID:
      label = bsdf_reflection_sample(sc,
                                     Ng,
                                     sd->I,
                                     sd->dI.dx,
                                     sd->dI.dy,
                                     randu,
                                     randv,
                                     eval,
                                     omega_in,
                                     &domega_in->dx,
                                     &domega_in->dy,
                                     pdf);
      break;
    case CLOSURE_BSDF_REFRACTION_ID:
      label = bsdf_refraction_sample(sc,
                                     Ng,
                                     sd->I,
                                     sd->dI.dx,
                                     sd->dI.dy,
                                     randu,
                                     randv,
                                     eval,
                                     omega_in,
                                     &domega_in->dx,
                                     &domega_in->dy,
                                     pdf);
      break;
    case CLOSURE_BSDF_TRANSPARENT_ID:
      label = bsdf_transparent_sample(sc,
                                      Ng,
                                      sd->I,
                                      sd->dI.dx,
                                      sd->dI.dy,
                                      randu,
                                      randv,
                                      eval,
                                      omega_in,
                                      &domega_in->dx,
                                      &domega_in->dy,
                                      pdf);
      break;
    case CLOSURE_BSDF_MICROFACET_GGX_ID:
    case CLOSURE_BSDF_MICROFACET_GGX_FRESNEL_ID:
    case CLOSURE_BSDF_MICROFACET_GGX_CLEARCOAT_ID:
    case CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID:
      label = bsdf_microfacet_ggx_sample(kg,
                                         sc,
                                         Ng,
                                         sd->I,
                                         sd->dI.dx,
                                         sd->dI.dy,
                                         randu,
                                         randv,
                                         eval,
                                         omega_in,
                                         &domega_in->dx,
                                         &domega_in->dy,
                                         pdf);
      break;
    case CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID:
    case CLOSURE_BSDF_MICROFACET_MULTI_GGX_FRESNEL_ID:
      label = bsdf_microfacet_multi_ggx_sample(kg,
                                               sc,
                                               Ng,
                                               sd->I,
                                               sd->dI.dx,
                                               sd->dI.dy,
                                               randu,
                                               randv,
                                               eval,
                                               omega_in,
                                               &domega_in->dx,
                                               &domega_in->dy,
                                               pdf,
                                               &sd->lcg_state);
      break;
    case CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID:
    case CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_FRESNEL_ID:
      label = bsdf_microfacet_multi_ggx_glass_sample(kg,
                                                     sc,
                                                     Ng,
                                                     sd->I,
                                                     sd->dI.dx,
                                                     sd->dI.dy,
                                                     randu,
                                                     randv,
                                                     eval,
                                                     omega_in,
                                                     &domega_in->dx,
                                                     &domega_in->dy,
                                                     pdf,
                                                     &sd->lcg_state);
      break;
    case CLOSURE_BSDF_MICROFACET_BECKMANN_ID:
    case CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID:
      label = bsdf_microfacet_beckmann_sample(kg,
                                              sc,
                                              Ng,
                                              sd->I,
                                              sd->dI.dx,
                                              sd->dI.dy,
                                              randu,
                                              randv,
                                              eval,
                                              omega_in,
                                              &domega_in->dx,
                                              &domega_in->dy,
                                              pdf);
      break;
    case CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID:
      label = bsdf_ashikhmin_shirley_sample(sc,
                                            Ng,
                                            sd->I,
                                            sd->dI.dx,
                                            sd->dI.dy,
                                            randu,
                                            randv,
                                            eval,
                                            omega_in,
                                            &domega_in->dx,
                                            &domega_in->dy,
                                            pdf);
      break;
    case CLOSURE_BSDF_ASHIKHMIN_VELVET_ID:
      label = bsdf_ashikhmin_velvet_sample(sc,
                                           Ng,
                                           sd->I,
                                           sd->dI.dx,
                                           sd->dI.dy,
                                           randu,
                                           randv,
                                           eval,
                                           omega_in,
                                           &domega_in->dx,
                                           &domega_in->dy,
                                           pdf);
      break;
    case CLOSURE_BSDF_DIFFUSE_TOON_ID:
      label = bsdf_diffuse_toon_sample(sc,
                                       Ng,
                                       sd->I,
                                       sd->dI.dx,
                                       sd->dI.dy,
                                       randu,
                                       randv,
                                       eval,
                                       omega_in,
                                       &domega_in->dx,
                                       &domega_in->dy,
                                       pdf);
      break;
    case CLOSURE_BSDF_GLOSSY_TOON_ID:
      label = bsdf_glossy_toon_sample(sc,
                                      Ng,
                                      sd->I,
                                      sd->dI.dx,
                                      sd->dI.dy,
                                      randu,
                                      randv,
                                      eval,
                                      omega_in,
                                      &domega_in->dx,
                                      &domega_in->dy,
                                      pdf);
      break;
    case CLOSURE_BSDF_HAIR_REFLECTION_ID:
      label = bsdf_hair_reflection_sample(sc,
                                          Ng,
                                          sd->I,
                                          sd->dI.dx,
                                          sd->dI.dy,
                                          randu,
                                          randv,
                                          eval,
                                          omega_in,
                                          &domega_in->dx,
                                          &domega_in->dy,
                                          pdf);
      break;
    case CLOSURE_BSDF_HAIR_TRANSMISSION_ID:
      label = bsdf_hair_transmission_sample(sc,
                                            Ng,
                                            sd->I,
                                            sd->dI.dx,
                                            sd->dI.dy,
                                            randu,
                                            randv,
                                            eval,
                                            omega_in,
                                            &domega_in->dx,
                                            &domega_in->dy,
                                            pdf);
      break;
    case CLOSURE_BSDF_HAIR_PRINCIPLED_ID:
      label = bsdf_principled_hair_sample(
          kg, sc, sd, randu, randv, eval, omega_in, &domega_in->dx, &domega_in->dy, pdf);
      break;
#  ifdef __PRINCIPLED__
    case CLOSURE_BSDF_PRINCIPLED_DIFFUSE_ID:
      label = bsdf_principled_diffuse_sample(sc,
                                             Ng,
                                             sd->I,
                                             sd->dI.dx,
                                             sd->dI.dy,
                                             randu,
                                             randv,
                                             eval,
                                             omega_in,
                                             &domega_in->dx,
                                             &domega_in->dy,
                                             pdf);
      break;
    case CLOSURE_BSDF_PRINCIPLED_SHEEN_ID:
      label = bsdf_principled_sheen_sample(sc,
                                           Ng,
                                           sd->I,
                                           sd->dI.dx,
                                           sd->dI.dy,
                                           randu,
                                           randv,
                                           eval,
                                           omega_in,
                                           &domega_in->dx,
                                           &domega_in->dy,
                                           pdf);
      break;
#  endif /* __PRINCIPLED__ */
#endif
    default:
      label = LABEL_NONE;
      break;
  }

  /* Test if BSDF sample should be treated as transparent for background. */
  if (label & LABEL_TRANSMIT) {
    float threshold_squared = kernel_data.background.transparent_roughness_squared_threshold;

    if (threshold_squared >= 0.0f && !(label & LABEL_DIFFUSE)) {
      if (bsdf_get_specular_roughness_squared(sc) <= threshold_squared) {
        label |= LABEL_TRANSMIT_TRANSPARENT;
      }
    }
  }
  else {
    /* Shadow terminator offset. */
    const float frequency_multiplier =
        kernel_data_fetch(objects, sd->object).shadow_terminator_shading_offset;
    if (frequency_multiplier > 1.0f) {
      *eval *= shift_cos_in(dot(*omega_in, sc->N), frequency_multiplier);
    }
    if (label & LABEL_DIFFUSE) {
      if (!isequal_float3(sc->N, sd->N)) {
        *eval *= bump_shadowing_term((label & LABEL_TRANSMIT) ? -sd->N : sd->N, sc->N, *omega_in);
      }
    }
  }

#ifdef WITH_CYCLES_DEBUG
  kernel_assert(*pdf >= 0.0f);
  kernel_assert(eval->x >= 0.0f && eval->y >= 0.0f && eval->z >= 0.0f);
#endif

  return label;
}

#ifndef __KERNEL_CUDA__
ccl_device
#else
ccl_device_inline
#endif
    float3
    bsdf_eval(KernelGlobals kg,
              ccl_private ShaderData *sd,
              ccl_private const ShaderClosure *sc,
              const float3 omega_in,
              const bool is_transmission,
              ccl_private float *pdf)
{
  float3 eval = zero_float3();

  if (!is_transmission) {
    switch (sc->type) {
      case CLOSURE_BSDF_DIFFUSE_ID:
        eval = bsdf_diffuse_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
#ifdef __SVM__
      case CLOSURE_BSDF_OREN_NAYAR_ID:
        eval = bsdf_oren_nayar_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
#  ifdef __OSL__
      case CLOSURE_BSDF_PHONG_RAMP_ID:
        eval = bsdf_phong_ramp_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_DIFFUSE_RAMP_ID:
        eval = bsdf_diffuse_ramp_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
#  endif
      case CLOSURE_BSDF_TRANSLUCENT_ID:
        eval = bsdf_translucent_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_REFLECTION_ID:
        eval = bsdf_reflection_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_REFRACTION_ID:
        eval = bsdf_refraction_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_TRANSPARENT_ID:
        eval = bsdf_transparent_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_MICROFACET_GGX_ID:
      case CLOSURE_BSDF_MICROFACET_GGX_FRESNEL_ID:
      case CLOSURE_BSDF_MICROFACET_GGX_CLEARCOAT_ID:
      case CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID:
        eval = bsdf_microfacet_ggx_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID:
      case CLOSURE_BSDF_MICROFACET_MULTI_GGX_FRESNEL_ID:
        eval = bsdf_microfacet_multi_ggx_eval_reflect(sc, sd->I, omega_in, pdf, &sd->lcg_state);
        break;
      case CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID:
      case CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_FRESNEL_ID:
        eval = bsdf_microfacet_multi_ggx_glass_eval_reflect(
            sc, sd->I, omega_in, pdf, &sd->lcg_state);
        break;
      case CLOSURE_BSDF_MICROFACET_BECKMANN_ID:
      case CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID:
        eval = bsdf_microfacet_beckmann_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID:
        eval = bsdf_ashikhmin_shirley_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_ASHIKHMIN_VELVET_ID:
        eval = bsdf_ashikhmin_velvet_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_DIFFUSE_TOON_ID:
        eval = bsdf_diffuse_toon_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_GLOSSY_TOON_ID:
        eval = bsdf_glossy_toon_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_HAIR_PRINCIPLED_ID:
        eval = bsdf_principled_hair_eval(kg, sd, sc, omega_in, pdf);
        break;
      case CLOSURE_BSDF_HAIR_REFLECTION_ID:
        eval = bsdf_hair_reflection_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_HAIR_TRANSMISSION_ID:
        eval = bsdf_hair_transmission_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
#  ifdef __PRINCIPLED__
      case CLOSURE_BSDF_PRINCIPLED_DIFFUSE_ID:
        eval = bsdf_principled_diffuse_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_PRINCIPLED_SHEEN_ID:
        eval = bsdf_principled_sheen_eval_reflect(sc, sd->I, omega_in, pdf);
        break;
#  endif /* __PRINCIPLED__ */
#endif
      default:
        break;
    }
    if (CLOSURE_IS_BSDF_DIFFUSE(sc->type)) {
      if (!isequal_float3(sc->N, sd->N)) {
        eval *= bump_shadowing_term(sd->N, sc->N, omega_in);
      }
    }
    /* Shadow terminator offset. */
    const float frequency_multiplier =
        kernel_data_fetch(objects, sd->object).shadow_terminator_shading_offset;
    if (frequency_multiplier > 1.0f) {
      eval *= shift_cos_in(dot(omega_in, sc->N), frequency_multiplier);
    }
  }
  else {
    switch (sc->type) {
      case CLOSURE_BSDF_DIFFUSE_ID:
        eval = bsdf_diffuse_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
#ifdef __SVM__
      case CLOSURE_BSDF_OREN_NAYAR_ID:
        eval = bsdf_oren_nayar_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_TRANSLUCENT_ID:
        eval = bsdf_translucent_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_REFLECTION_ID:
        eval = bsdf_reflection_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_REFRACTION_ID:
        eval = bsdf_refraction_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_TRANSPARENT_ID:
        eval = bsdf_transparent_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_MICROFACET_GGX_ID:
      case CLOSURE_BSDF_MICROFACET_GGX_FRESNEL_ID:
      case CLOSURE_BSDF_MICROFACET_GGX_CLEARCOAT_ID:
      case CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID:
        eval = bsdf_microfacet_ggx_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID:
      case CLOSURE_BSDF_MICROFACET_MULTI_GGX_FRESNEL_ID:
        eval = bsdf_microfacet_multi_ggx_eval_transmit(sc, sd->I, omega_in, pdf, &sd->lcg_state);
        break;
      case CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID:
      case CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_FRESNEL_ID:
        eval = bsdf_microfacet_multi_ggx_glass_eval_transmit(
            sc, sd->I, omega_in, pdf, &sd->lcg_state);
        break;
      case CLOSURE_BSDF_MICROFACET_BECKMANN_ID:
      case CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID:
        eval = bsdf_microfacet_beckmann_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID:
        eval = bsdf_ashikhmin_shirley_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_ASHIKHMIN_VELVET_ID:
        eval = bsdf_ashikhmin_velvet_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_DIFFUSE_TOON_ID:
        eval = bsdf_diffuse_toon_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_GLOSSY_TOON_ID:
        eval = bsdf_glossy_toon_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_HAIR_PRINCIPLED_ID:
        eval = bsdf_principled_hair_eval(kg, sd, sc, omega_in, pdf);
        break;
      case CLOSURE_BSDF_HAIR_REFLECTION_ID:
        eval = bsdf_hair_reflection_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_HAIR_TRANSMISSION_ID:
        eval = bsdf_hair_transmission_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
#  ifdef __PRINCIPLED__
      case CLOSURE_BSDF_PRINCIPLED_DIFFUSE_ID:
        eval = bsdf_principled_diffuse_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
      case CLOSURE_BSDF_PRINCIPLED_SHEEN_ID:
        eval = bsdf_principled_sheen_eval_transmit(sc, sd->I, omega_in, pdf);
        break;
#  endif /* __PRINCIPLED__ */
#endif
      default:
        break;
    }
    if (CLOSURE_IS_BSDF_DIFFUSE(sc->type)) {
      if (!isequal_float3(sc->N, sd->N)) {
        eval *= bump_shadowing_term(-sd->N, sc->N, omega_in);
      }
    }
  }
#ifdef WITH_CYCLES_DEBUG
  kernel_assert(*pdf >= 0.0f);
  kernel_assert(eval.x >= 0.0f && eval.y >= 0.0f && eval.z >= 0.0f);
#endif
  return eval;
}

ccl_device void bsdf_blur(KernelGlobals kg, ccl_private ShaderClosure *sc, float roughness)
{
  /* TODO: do we want to blur volume closures? */
#ifdef __SVM__
  switch (sc->type) {
    case CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID:
    case CLOSURE_BSDF_MICROFACET_MULTI_GGX_FRESNEL_ID:
    case CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID:
    case CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_FRESNEL_ID:
      bsdf_microfacet_multi_ggx_blur(sc, roughness);
      break;
    case CLOSURE_BSDF_MICROFACET_GGX_ID:
    case CLOSURE_BSDF_MICROFACET_GGX_FRESNEL_ID:
    case CLOSURE_BSDF_MICROFACET_GGX_CLEARCOAT_ID:
    case CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID:
      bsdf_microfacet_ggx_blur(sc, roughness);
      break;
    case CLOSURE_BSDF_MICROFACET_BECKMANN_ID:
    case CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID:
      bsdf_microfacet_beckmann_blur(sc, roughness);
      break;
    case CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID:
      bsdf_ashikhmin_shirley_blur(sc, roughness);
      break;
    case CLOSURE_BSDF_HAIR_PRINCIPLED_ID:
      bsdf_principled_hair_blur(sc, roughness);
      break;
    default:
      break;
  }
#endif
}

CCL_NAMESPACE_END
