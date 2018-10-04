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
  hw_get_module(HWC_HARDWARE_MODULE_ID, &hybris_plugin_hwc_handle);
  if( !hybris_plugin_hwc_handle ) {
    mce_log(LL_DEBUG, "failed to open hw composer module");
  }

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
static framebuffer_device_t *hybris_device_fb_handle = 0;

/** Pointer to libhybris frame buffer device object */
static hwc_composer_device_1_t *hybris_device_hwc_handle = 0;

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

  /* Open frame buffer device */
  if( hybris_plugin_fb_handle ) {
    hybris_plugin_fb_handle->methods->open(hybris_plugin_fb_handle,
                                           GRALLOC_HARDWARE_FB0,
                                           (hw_device_t**)&hybris_device_fb_handle);
    if( !hybris_device_fb_handle ) {
      mce_log(LL_WARN, "failed to open frame buffer device");
    }
  }

  /* Open hw composer device */
  if( hybris_plugin_hwc_handle ) {
    hybris_plugin_hwc_handle->methods->open(hybris_plugin_hwc_handle,
                                            HWC_HARDWARE_COMPOSER,
                                            (hw_device_t**)&hybris_device_hwc_handle);
    if( !hybris_device_hwc_handle ) {
      mce_log(LL_WARN, "failed to open hw composer device");
    }
  }

  /* What we'd like to use is:
   *
   * 1. hwc_dev->setPowerMode()
   * 2. hwc_dev->blank()
   * 2. fb_dev->enableScreen()
   *
   * While all are optional, having none available is unexpected.
   */

  if( hybris_device_hwc_handle ) {
#ifdef HWC_DEVICE_API_VERSION_1_4
    if( hybris_device_hwc_handle->common.version >= HWC_DEVICE_API_VERSION_1_4 &&
        hybris_device_hwc_handle->setPowerMode ) {
      mce_log(LL_DEBUG, "using hw composer setPowerMode() method");
      ack = true;
      goto cleanup;
    }
#endif
    if( hybris_device_hwc_handle->blank ) {
      mce_log(LL_DEBUG, "using hw composer blank() method");
      ack = true;
      goto cleanup;
    }
  }

  if( hybris_device_fb_handle ) {
    if( hybris_device_fb_handle->enableScreen ) {
      mce_log(LL_DEBUG, "using framebuffer enableScreen() method");
      ack = true;
      goto cleanup;
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
    hybris_device_hwc_handle->common.close(&hybris_device_hwc_handle->common);
    hybris_device_hwc_handle = 0;
  }

  if( hybris_device_fb_handle ) {
    hybris_device_fb_handle->common.close(&hybris_device_fb_handle->common);
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
  bool ack = false;

  if( !hybris_device_fb_init() ) {
    goto cleanup;
  }

  /* Try hwc methods */
  if( hybris_device_hwc_handle ) {
#ifdef HWC_DEVICE_API_VERSION_1_4
    if( hybris_device_hwc_handle->common.version >= HWC_DEVICE_API_VERSION_1_4 &&
        hybris_device_hwc_handle->setPowerMode ) {
      int disp = 0;
      int mode = state ? HWC_POWER_MODE_NORMAL : HWC_POWER_MODE_OFF;
      int err = hybris_device_hwc_handle->setPowerMode(hybris_device_hwc_handle,
                                                       disp, mode);
      mce_log(err ? LL_WARN : LL_DEBUG, "setPowerMode(%d, %d) -> err=%d ",
              disp, mode, err);
      ack = !err;
      goto cleanup;
    }
#endif
    if( hybris_device_hwc_handle->blank ) {
      int disp = 0;
      int blank = state ? false : true;
      int err = hybris_device_hwc_handle->blank(hybris_device_hwc_handle,
                                                disp, blank);
      mce_log(err ? LL_WARN : LL_DEBUG, "blank(%d, %d) -> err=%d ",
              disp, blank, err);
      ack = !err;
      goto cleanup;
    }
  }

  /* Try fb methods */
  if( hybris_device_fb_handle ) {
    if( hybris_device_fb_handle->enableScreen ) {
      int err = hybris_device_fb_handle->enableScreen(hybris_device_fb_handle,
                                                      state);
      mce_log(err ? LL_WARN : LL_DEBUG, "enableScreen(%d) -> err=%d ",
              state, err);
      ack = !err;
      goto cleanup;
    }
  }

  /* Failed */
  mce_log(LL_WARN, "no known display power control interfaces");

cleanup:

  return ack;
}
