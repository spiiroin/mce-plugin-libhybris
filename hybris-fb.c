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
#include <hardware/fb.h>

/* ========================================================================= *
 * PROTOTYPES
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * FRAMEBUFFER_PLUGIN
 * ------------------------------------------------------------------------- */

bool        hybris_plugin_fb_load         (void);
void        hybris_plugin_fb_unload       (void);

static int  hybris_plugin_fb_open_device  (struct framebuffer_device_t **pdevice);
static void hybris_plugin_fb_close_device (struct framebuffer_device_t **pdevice);

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

  hw_get_module(GRALLOC_HARDWARE_FB0, &hybris_plugin_fb_handle);

  if( !hybris_plugin_fb_handle ) {
    mce_log(LL_WARN, "failed to open frame buffer module");
    goto cleanup;
  }

  mce_log(LL_DEBUG, "hybris_plugin_fb_handle = %p",
          hybris_plugin_fb_handle);

cleanup:

  return hybris_plugin_fb_handle != 0;
}

/** Unload libhybris framebuffer plugin
 */
void
hybris_plugin_fb_unload(void)
{
  /* Close devices related to the plugin */
  hybris_device_fb_quit();

  /* actually unload the module */
  // FIXME: how to unload libhybris modules?
}

/** Convenience function for opening frame buffer device
 *
 * Similar to what we might or might not have available from hardware/fb.h
 */
static int
hybris_plugin_fb_open_device(struct framebuffer_device_t ** pdevice)
{
  int ack = false;

  if( !hybris_plugin_fb_load() ) {
    goto cleanup;
  }

  const struct hw_module_t *mod = hybris_plugin_fb_handle;
  const char               *id  = GRALLOC_HARDWARE_FB0;
  struct hw_device_t       *dev = 0;

  if( !mod->methods->open(mod, id, &dev) ) {
    goto cleanup;
  }

  *pdevice = (struct framebuffer_device_t*)dev;

  ack = true;

cleanup:

  return ack;
}

/** Convenience function for closing frame buffer device
 *
 * Similar to what we might or might not have available from hardware/fb.h
 */
static void
hybris_plugin_fb_close_device(struct framebuffer_device_t **pdevice)
{
  struct framebuffer_device_t *device;

  if( (device = *pdevice) ) {
    *pdevice = 0, device->common.close(&device->common);
  }
}

/* ========================================================================= *
 * FRAMEBUFFER_DEVICE
 * ========================================================================= */

/** Pointer to libhybris frame buffer device object */
static struct framebuffer_device_t *hybris_device_fb_handle = 0;

/** Initialize libhybris frame buffer device object
 *
 * @return true on success, false on failure
 */
bool
hybris_device_fb_init(void)
{
  static bool done = false;

  if( done ) {
    goto cleanup;
  }

  done = true;

  hybris_plugin_fb_open_device(&hybris_device_fb_handle);

  if( !hybris_device_fb_handle ) {
    mce_log(LL_ERR, "failed to open framebuffer device");
    goto cleanup;
  }

  mce_log(LL_DEBUG, "hybris_device_fb_handle = %p",
          hybris_device_fb_handle);

cleanup:

  return hybris_device_fb_handle != 0;
}

/** Release libhybris frame buffer device object
 */
void
hybris_device_fb_quit(void)
{
  hybris_plugin_fb_close_device(&hybris_device_fb_handle);
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

  if( hybris_device_fb_handle->enableScreen(hybris_device_fb_handle, state) < 0 ) {
    goto cleanup;
  }

  ack = true;

cleanup:

  return ack;
}
