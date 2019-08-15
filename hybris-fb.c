/** @file hybris-fb.c
 *
 * mce-plugin-libhybris - Libhybris plugin for Mode Control Entity
 * <p>
 * Copyright (C) 2013-2017 Jolla Ltd.
 * <p>
 * @author Simo Piiroinen <simo.piiroinen@jollamobile.com>
 *
 * mce-plugin-libhybris is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License.
 *
 * mce-plugin-libhybris is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with mce-plugin-libhybris; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "hybris-fb.h"
#include "plugin-logging.h"

#include "plugin-api.h"

#include <system/window.h>
#include <hardware/gralloc.h>
#include <hardware/fb.h>
#include <hardware/hwcomposer.h>

/* ========================================================================= *
 * Types
 * ========================================================================= */

/* When building for older devices, hwcomposer2.h header might not
 * be available -> duplicate essential bits here, warts and all.
 */

typedef enum {
  HWC2_FUNCTION_SET_POWER_MODE = 41,
} hwc2_function_descriptor_t;

typedef void (*hwc2_function_pointer_t)();

typedef uint64_t hwc2_display_t;

typedef struct hwc2_device
{
    struct hw_device_t common;
    void (*getCapabilities)(struct hwc2_device *device,
                            uint32_t *outCount,
                            int32_t *outCapabilities);
    hwc2_function_pointer_t (*getFunction)(struct hwc2_device *device,
                                           int32_t descriptor);
} hwc2_device_t;

typedef int32_t (*HWC2_PFN_SET_POWER_MODE)(hwc2_device_t *device,
                                           hwc2_display_t display,
                                           int32_t mode);

/* ========================================================================= *
 * PROTOTYPES
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * FRAMEBUFFER_PLUGIN
 * ------------------------------------------------------------------------- */

bool        hybris_plugin_fb_load         (void);
void        hybris_plugin_fb_unload       (void);

/* ------------------------------------------------------------------------- *
 * FRAMEBUFFER_DEVICE
 * ------------------------------------------------------------------------- */

bool        hybris_device_fb_init         (void);
void        hybris_device_fb_quit         (void);
bool        hybris_device_fb_set_power    (bool state);

/* ========================================================================= *
 * FRAMEBUFFER_PLUGIN
 * ========================================================================= */

/** Handle for libhybris framebuffer plugin */
static const  struct hw_module_t *hybris_plugin_fb_handle = 0;

/** Handle for libhybris hw composer plugin */
static const  struct hw_module_t *hybris_plugin_hwc_handle = 0;

/** Load libhybris framebuffer plugin
 *
 * @return true on success, false on failure
 */
bool
hybris_plugin_fb_load(void)
{
  static bool done = false;

  if( done ) {
    goto cleanup;
  }

  done = true;

  /* Load framebuffer module */
  hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &hybris_plugin_fb_handle);
  if( !hybris_plugin_fb_handle ) {
    mce_log(LL_DEBUG, "failed to open frame buffer module");
  }

  /* Load hw composer module */
#ifdef HWC_DEVICE_API_VERSION_1_0
  hw_get_module(HWC_HARDWARE_MODULE_ID, &hybris_plugin_hwc_handle);
  if( !hybris_plugin_hwc_handle ) {
    mce_log(LL_DEBUG, "failed to open hw composer module");
  }
#else
  /* When compiling with really old hwcompser.h in place, assume
   * that the target device is not going to have the hwc methods we
   * are interested in -> skip module load -> hwc device loading and
   * related cleanup turns into a nop without further #ifdeffing.
   */
#endif

  /* Both fb and hwc are optional, but having neither is unexpected */
  if( !hybris_plugin_fb_handle && !hybris_plugin_hwc_handle ) {
    mce_log(LL_WARN, "could not open neither fb nor hwc module");
  }

cleanup:
  return hybris_plugin_fb_handle || hybris_plugin_hwc_handle;
}

/** Unload libhybris framebuffer plugin
 */
void
hybris_plugin_fb_unload(void)
{
  /* Close open devices */
  hybris_device_fb_quit();

  /* Unload modules */
  // FIXME: how to unload libhybris modules?
  hybris_plugin_fb_handle = 0;
  hybris_plugin_hwc_handle = 0;
}

/* ========================================================================= *
 * FRAMEBUFFER_DEVICE
 * ========================================================================= */

/** Pointer to libhybris frame buffer device object */
static hw_device_t *hybris_device_fb_handle = 0;

/** Pointer to libhybris frame buffer device object */
static hw_device_t *hybris_device_hwc_handle = 0;

/** Initialize libhybris frame buffer device object
 *
 * @return true on success, false on failure
 */
bool
hybris_device_fb_init(void)
{
  static bool ack = false;
  static bool done = false;

  if( done ) {
    goto cleanup;
  }

  done = true;

  if( !hybris_plugin_fb_load() ) {
    goto cleanup;
  }

  /* What we'd like to use is:
   *
   * 1. hwc_dev->setPowerMode()
   * 2. hwc_dev->blank()
   * 2. fb_dev->enableScreen()
   *
   * While all are optional, having none available is unexpected.
   */

  /* Probe hw composer device */
  if( hybris_plugin_hwc_handle ) {
    hybris_plugin_hwc_handle->methods->open(hybris_plugin_hwc_handle,
                                            HWC_HARDWARE_COMPOSER,
                                            &hybris_device_hwc_handle);
    if( !hybris_device_hwc_handle ) {
      mce_log(LL_WARN, "failed to open hw composer device");
    }
    else {
      uint32_t vers = hybris_device_hwc_handle->version >> 16;
      mce_log(LL_DEBUG, "hwc version: %u.%u", (vers >> 8), (vers & 255));
      if( vers >= 0x0300 ) {
        mce_log(LL_WARN, "hwc api level 3+ - not supported");
      }
      else if( vers >= 0x0200 ) {
        hwc2_device_t *hwcdev = (hwc2_device_t *)hybris_device_hwc_handle;
        if( hwcdev->getFunction ) {
          if( hwcdev->getFunction(hwcdev, HWC2_FUNCTION_SET_POWER_MODE) ) {
            mce_log(LL_DEBUG, "using hw composer 2.0 setPowerMode() method");
            ack = true;
            goto cleanup;
          }
        }
        mce_log(LL_WARN, "hwc api level 2.0 - setPowerMode() not available");
      }
      else if( vers >= 0x0104 ) {
#ifdef HWC_DEVICE_API_VERSION_1_4
        hwc_composer_device_1_t *hwcdev = (hwc_composer_device_1_t *)hybris_device_hwc_handle;
        if( hwcdev->setPowerMode ) {
          mce_log(LL_DEBUG, "using hw composer 1.4 setPowerMode() method");
          ack = true;
          goto cleanup;
        }
#endif
        mce_log(LL_WARN, "hwc api level 1.4 - setPowerMode() not available");
      }
      else if( vers >= 0x0100 ) {
#ifdef HWC_DEVICE_API_VERSION_1_0
        hwc_composer_device_1_t *hwcdev = (hwc_composer_device_1_t *)hybris_device_hwc_handle;
        if( hwcdev->blank ) {
          mce_log(LL_DEBUG, "using hw composer 1.0 blank() method");
          ack = true;
          goto cleanup;
        }
#endif
        mce_log(LL_WARN, "hwc api level 1.0 - blank() not available");
      }
      else {
        mce_log(LL_WARN, "hwc api level 0 - not supported");
      }

      /* Nothing usable available -> close device */
      hybris_device_hwc_handle->close(hybris_device_hwc_handle),
      hybris_device_hwc_handle = 0;
    }
  }

  /* Probe frame buffer device */
  if( hybris_plugin_fb_handle ) {
    hybris_plugin_fb_handle->methods->open(hybris_plugin_fb_handle,
                                           GRALLOC_HARDWARE_FB0,
                                           &hybris_device_fb_handle);
    if( !hybris_device_fb_handle ) {
      mce_log(LL_WARN, "failed to open frame buffer device");
    }
    else {
      uint32_t vers = hybris_device_fb_handle->version >> 16;
      mce_log(LL_DEBUG, "fb_device version: %u.%u", (vers >> 8), (vers & 255));

      framebuffer_device_t *fbdev = (framebuffer_device_t *)hybris_device_fb_handle;

      if( fbdev->enableScreen ) {
        mce_log(LL_DEBUG, "using framebuffer enableScreen() method");
        ack = true;
        goto cleanup;
      }
      mce_log(LL_WARN, "fb api - enableScreen() not available");

      /* Nothing usable available -> close device */
      hybris_device_fb_handle->close(hybris_device_fb_handle),
      hybris_device_fb_handle = 0;
    }
  }

  mce_log(LL_WARN, "no known display power control interfaces");

cleanup:

  return ack;
}

/** Release libhybris frame buffer device object
 */
void
hybris_device_fb_quit(void)
{
  if( hybris_device_hwc_handle ) {
    hybris_device_hwc_handle->close(hybris_device_hwc_handle),
    hybris_device_hwc_handle = 0;
  }

  if( hybris_device_fb_handle ) {
    hybris_device_fb_handle->close(hybris_device_fb_handle),
    hybris_device_fb_handle = 0;
  }
}

/** Set frame buffer power state via libhybris
 *
 * @param state true to power on, false to power off
 *
 * @return true on success, false on failure
 */
bool
hybris_device_fb_set_power(bool state)
{
  int err = -1;

  if( !hybris_device_fb_init() ) {
    goto cleanup;
  }

  /* Try hwc methods */
  if( hybris_device_hwc_handle ) {
    int disp  = 0;
    int mode  = state ? HWC_POWER_MODE_NORMAL : HWC_POWER_MODE_OFF;
    int blank = state ? false : true;

    uint32_t vers = hybris_device_hwc_handle->version >> 16;
    mce_log(LL_DEBUG, "hwc_device version: %u.%u", (vers >> 8), (vers & 255));

    if( vers >= 0x0300 ) {
      /* We do not know what API v3+ might be like -> NOP */
    }
    else if( vers >= 0x0200 ) {
      hwc2_device_t *hwcdev = (hwc2_device_t *)hybris_device_hwc_handle;
      if( hwcdev->getFunction ) {
        hwc2_function_pointer_t a_function = hwcdev->getFunction(hwcdev, HWC2_FUNCTION_SET_POWER_MODE);
        HWC2_PFN_SET_POWER_MODE the_function = (HWC2_PFN_SET_POWER_MODE)(void *)a_function;
        if( the_function ) {
          err = the_function(hwcdev, disp, mode);
          mce_log(err ? LL_WARN : LL_DEBUG,
                  "hw composer 2.0 setPowerMode(%d) -> err=%d",
                  mode, err);
        }
      }
    }
    else if( vers >= 0x0104 ) {
#ifdef HWC_DEVICE_API_VERSION_1_4
      hwc_composer_device_1_t *hwcdev = (hwc_composer_device_1_t *)hybris_device_hwc_handle;
      if( hwcdev->setPowerMode ) {
        err = hwcdev->setPowerMode(hwcdev, disp, mode);
        mce_log(err ? LL_WARN : LL_DEBUG,
                "hw composer 1.4 setPowerMode(%d) -> err=%d",
                mode, err);
      }
#endif
    }
    else if( vers >= 0x0100 ) {
#ifdef HWC_DEVICE_API_VERSION_1_0
      hwc_composer_device_1_t *hwcdev = (hwc_composer_device_1_t *)hybris_device_hwc_handle;
      if( hwcdev->blank ) {
        err = hwcdev->blank(hwcdev, disp, blank);
        mce_log(err ? LL_WARN : LL_DEBUG,
                "hw composer 1.0 blank(%d) -> err=%d",
                blank, err);
      }
#endif
    }
    else {
      /* We have no use for API v0 -> NOP */
    }
  }
  /* Try fb methods */
  else if( hybris_device_fb_handle ) {
    framebuffer_device_t *fbdev = (framebuffer_device_t *)hybris_device_fb_handle;
    if( fbdev->enableScreen ) {
      err = fbdev->enableScreen(fbdev, state);
      mce_log(err ? LL_WARN : LL_DEBUG,
              "frame buffer enableScreen(%d) -> err=%d",
              state, err);
    }
  }
  /* Failed */
  else {
    /* We already did a warning when probing */
    mce_log(LL_DEBUG, "no known display power control interfaces");
  }

cleanup:

  return (err == 0);
}
