/** @file hybris-lights.c
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

#include "hybris-lights.h"
#include "plugin-logging.h"

#include "plugin-api.h"

#include <system/window.h>
#include <hardware/lights.h>

/* ========================================================================= *
 * PROTOTYPES
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * UTILITIES
 * ------------------------------------------------------------------------- */

static int  clamp_to_range                        (int lo, int hi, int val);

/* ------------------------------------------------------------------------- *
 * LIGHTS_PLUGIN
 * ------------------------------------------------------------------------- */

bool        hybris_plugin_lights_load             (void);
void        hybris_plugin_lights_unload           (void);

static int  hybris_plugin_lights_open_device      (const char *id, struct light_device_t **pdevice);
static void hybris_plugin_lights_close_device     (struct light_device_t **pdevice);

/* ------------------------------------------------------------------------- *
 * DISPLAY_BACKLIGHT
 * ------------------------------------------------------------------------- */

bool        hybris_device_backlight_init          (void);
void        hybris_device_backlight_quit          (void);
bool        hybris_device_backlight_set_brightness(int level);

/* ------------------------------------------------------------------------- *
 * KEYBOARD_BACKLIGHT
 * ------------------------------------------------------------------------- */

bool        hybris_device_keypad_init             (void);
void        hybris_device_keypad_quit             (void);
bool        hybris_device_keypad_set_brightness   (int level);

/* ------------------------------------------------------------------------- *
 * INDICATOR_LED
 * ------------------------------------------------------------------------- */

bool        hybris_device_indicator_init          (void);
void        hybris_device_indicator_quit          (void);
bool        hybris_device_indicator_set_pattern   (int r, int g, int b, int ms_on, int ms_off);

/* ========================================================================= *
 * UTILITIES
 * ========================================================================= */

/** Clamp integer values to given range
 *
 * @param lo  minimum value allowed
 * @param hi  maximum value allowed
 * @param val value to clamp
 *
 * @return val clamped to [lo, hi]
 */
static int
clamp_to_range(int lo, int hi, int val)
{
  return val <= lo ? lo : val <= hi ? val : hi;
}

/* ========================================================================= *
 * LIGHTS_PLUGIN
 * ========================================================================= */

/** Handle for libhybris lights plugin */
static const struct hw_module_t *hybris_plugin_lights_handle    = 0;

/** Load libhybris lights plugin
 *
 * @return true on success, false on failure
 */
bool
hybris_plugin_lights_load(void)
{
  static bool done = false;

  if( done ) {
    goto cleanup;
  }

  done = true;

  hw_get_module(LIGHTS_HARDWARE_MODULE_ID, &hybris_plugin_lights_handle);
  if( !hybris_plugin_lights_handle ) {
    mce_log(LL_WARN, "failed to open lights module");
    goto cleanup;
  }

  mce_log(LL_DEBUG, "hybris_plugin_lights_handle = %p",
          hybris_plugin_lights_handle);

cleanup:

  return hybris_plugin_lights_handle != 0;
}

/** Unload libhybris lights plugin
 */
void
hybris_plugin_lights_unload(void)
{
  /* cleanup dependencies */
  hybris_device_backlight_quit();
  hybris_device_keypad_quit();
  hybris_device_indicator_quit();

  /* actually unload the module */
  // FIXME: how to unload libhybris modules?
}

/** Convenience function for opening a light device
 *
 * Similar to what we might or might not have available from hardware/lights.h
 */
static int
hybris_plugin_lights_open_device(const char *id, struct light_device_t **pdevice)
{
  int ack = false;

  if( !id || !pdevice ) {
    goto cleanup;
  }

  if( !hybris_plugin_lights_load() ) {
    goto cleanup;
  }

  const struct hw_module_t *module = hybris_plugin_lights_handle;

  if( !module->methods->open(module, id, (struct hw_device_t**)pdevice) ) {
    goto cleanup;
  }

  ack = true;

cleanup:

  return ack;
}

/** Convenience function for closing a light device
 *
 * Similar to what we might or might not have available from hardware/lights.h
 */
static void
hybris_plugin_lights_close_device(struct light_device_t **pdevice)
{
  struct light_device_t *device;

  if( (device = *pdevice) ) {
    *pdevice = 0, device->common.close(&device->common);
  }
}

/* ========================================================================= *
 * DISPLAY_BACKLIGHT
 * ========================================================================= */

/** Pointer to libhybris frame display backlight device object */
static struct light_device_t    *hybris_device_backlight_handle = 0;

/** Initialize libhybris display backlight device object
 *
 * @return true on success, false on failure
 */
bool
hybris_device_backlight_init(void)
{
  static bool done = false;

  if( done ) {
    goto cleanup;
  }

  done = true;

  hybris_plugin_lights_open_device(LIGHT_ID_BACKLIGHT,
                                   &hybris_device_backlight_handle);

  if( !hybris_device_backlight_handle ) {
    mce_log(LL_WARN, "failed to open backlight device");
    goto cleanup;
  }

  mce_log(LL_DEBUG, "hybris_device_backlight_handle -> %p",
          hybris_device_backlight_handle);

cleanup:
  return hybris_device_backlight_handle != 0;
}

/** Release libhybris display backlight device object
 */
void
hybris_device_backlight_quit(void)
{
  hybris_plugin_lights_close_device(&hybris_device_backlight_handle);
}

/** Set display backlight brightness via libhybris
 *
 * @param level 0=off ... 255=maximum brightness
 *
 * @return true on success, false on failure
 */
bool
hybris_device_backlight_set_brightness(int level)
{
  bool ack = false;

  if( !hybris_device_backlight_init() ) {
    goto cleanup;
  }

  unsigned lev = clamp_to_range(0, 255, level);

  struct light_state_t lst;

  memset(&lst, 0, sizeof lst);

  lst.color          = (0xff << 24) | (lev << 16) | (lev << 8) | (lev << 0);
  lst.flashMode      = LIGHT_FLASH_NONE;
  lst.flashOnMS      = 0;
  lst.flashOffMS     = 0;
  lst.brightnessMode = BRIGHTNESS_MODE_USER;

  if( hybris_device_backlight_handle->set_light(hybris_device_backlight_handle, &lst) < 0 ) {
    goto cleanup;
  }

  ack = true;

cleanup:

  mce_log(LL_DEBUG, "brightness(%d) -> %s",
          level, ack ? "success" : "failure");

  return ack;
}

/* ========================================================================= *
 * KEYBOARD_BACKLIGHT
 * ========================================================================= */

/** Pointer to libhybris frame keypad backlight device object */
static struct light_device_t    *hybris_device_keypad_handle    = 0;

/** Initialize libhybris keypad backlight device object
 *
 * @return true on success, false on failure
 */
bool
hybris_device_keypad_init(void)
{
  static bool done = false;

  if( done ) {
    goto cleanup;
  }

  done = true;

  hybris_plugin_lights_open_device(LIGHT_ID_KEYBOARD, &hybris_device_keypad_handle);

  if( !hybris_device_keypad_handle ) {
    mce_log(LL_WARN, "failed to open keypad backlight device");
    goto cleanup;
  }

  mce_log(LL_DEBUG, "hybris_device_keypad_handle -> %p",
          hybris_device_keypad_handle);

cleanup:

  return hybris_device_keypad_handle != 0;
}

/** Release libhybris keypad backlight device object
 */
void
hybris_device_keypad_quit(void)
{
  hybris_plugin_lights_close_device(&hybris_device_keypad_handle);
}

/** Set display keypad brightness via libhybris
 *
 * @param level 0=off ... 255=maximum brightness
 *
 * @return true on success, false on failure
 */
bool
hybris_device_keypad_set_brightness(int level)
{
  bool ack = false;

  if( !hybris_device_keypad_init() ) {
    goto cleanup;
  }

  unsigned lev = clamp_to_range(0, 255, level);

  struct light_state_t lst;

  memset(&lst, 0, sizeof lst);

  lst.color          = (0xff << 24) | (lev << 16) | (lev << 8) | (lev << 0);
  lst.flashMode      = LIGHT_FLASH_NONE;
  lst.flashOnMS      = 0;
  lst.flashOffMS     = 0;
  lst.brightnessMode = BRIGHTNESS_MODE_USER;

  if( hybris_device_keypad_handle->set_light(hybris_device_keypad_handle, &lst) < 0 ) {
    goto cleanup;
  }

  ack = true;

cleanup:

  mce_log(LL_DEBUG, "brightness(%d) -> %s",
          level, ack ? "success" : "failure");

  return ack;
}

/* ========================================================================= *
 * INDICATOR_LED
 * ========================================================================= */

/** Pointer to libhybris frame indicator led device object */
static struct light_device_t    *hybris_device_indicator_handle = 0;

/** Initialize libhybris indicator led device object
 *
 * @return true on success, false on failure
 */
bool
hybris_device_indicator_init(void)
{
  static bool done = false;

  if( done ) {
    goto cleanup;
  }

  done = true;

  hybris_plugin_lights_open_device(LIGHT_ID_NOTIFICATIONS,
                                   &hybris_device_indicator_handle);

  if( !hybris_device_indicator_handle ) {
    mce_log(LL_WARN, "failed to open indicator led device");
    goto cleanup;
  }

  mce_log(LL_DEBUG, "hybris_device_indicator_handle -> %p",
          hybris_device_indicator_handle);

cleanup:

  return hybris_device_indicator_handle != 0;
}

/** Release libhybris indicator led device object
 */
void
hybris_device_indicator_quit(void)
{
  hybris_plugin_lights_close_device(&hybris_device_indicator_handle);
}

/** Set indicator led pattern via libhybris
 *
 * @param r     red intensity 0 ... 255
 * @param g     green intensity 0 ... 255
 * @param b     blue intensity 0 ... 255
 * @param ms_on milliseconds to keep the led on, or 0 for no flashing
 * @param ms_on milliseconds to keep the led off, or 0 for no flashing
 *
 * @return true on success, false on failure
 */
bool
hybris_device_indicator_set_pattern(int r, int g, int b, int ms_on, int ms_off)
{
  bool ack = false;

  if( !hybris_device_indicator_init() ) {
    goto cleanup;
  }

  r = clamp_to_range(0, 255, r);
  g = clamp_to_range(0, 255, g);
  b = clamp_to_range(0, 255, b);

  ms_on  = clamp_to_range(0, 60000, ms_on);
  ms_off = clamp_to_range(0, 60000, ms_off);

  if( ms_on < 50 || ms_off < 50 ) {
    ms_on = ms_off = 0;
  }

  struct light_state_t lst;

  memset(&lst, 0, sizeof lst);

  lst.color          = (0xff << 24) | (r << 16) | (g << 8) | (b << 0);
  lst.brightnessMode = BRIGHTNESS_MODE_USER;

  if( ms_on > 0 && ms_off > 0 ) {
    lst.flashMode    = LIGHT_FLASH_HARDWARE;
    lst.flashOnMS    = ms_on;
    lst.flashOffMS   = ms_off;
  }
  else {
    lst.flashMode    = LIGHT_FLASH_NONE;
    lst.flashOnMS    = 0;
    lst.flashOffMS   = 0;
  }

  if( hybris_device_indicator_handle->set_light(hybris_device_indicator_handle, &lst) < 0 ) {
    goto cleanup;
  }

  ack = true;

cleanup:

  mce_log(LL_DEBUG, "pattern(%d,%d,%d,%d,%d) -> %s",
          r,g,b, ms_on, ms_off , ack ? "success" : "failure");

  return ack;
}
