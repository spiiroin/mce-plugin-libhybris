/** @file plugin-api.c
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

/* ========================================================================= *
 * This module implements "mce-libhybris-plugin" for use from
 * "mce-libhybris-module" within mce.
 *
 * The idea of "hybris-plugin" is shortly:
 * - it uses no mce functions or data types
 * - it can be compiled independently from mce
 * - it exposes no libhybris/android datatypes / functions
 *
 * And the idea of "hybris-module" is:
 * - it contains functions with the same names as "hybris-plugin"
 * - if called, the functions will load & call "hybris-plugin" code
 * - if "hybris-plugin" is not present "hybris-module" functions
 *   still work, but return failures for everything
 *
 * Put together:
 * - mce code can assume that libhybris code is always available and
 *   callable during hw probing activity
 * - if hybris plugin is not installed (or if some hw is not supported
 *   by the underlying android code), failures will be reported and mce
 *   can try other existing ways to proble hw controls
 * ========================================================================= */

#include "plugin-api.h"

#include "plugin-logging.h"
#include "hybris-fb.h"
#include "hybris-lights.h"
#include "hybris-sensors.h"

#include "sysfs-led-main.h"

/* ========================================================================= *
 * PROTOTYPES
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * FRAME_BUFFER_POWER_STATE
 * ------------------------------------------------------------------------- */

bool mce_hybris_framebuffer_init          (void);
void mce_hybris_framebuffer_quit          (void);
bool mce_hybris_framebuffer_set_power     (bool state);

/* ------------------------------------------------------------------------- *
 * DISPLAY_BACKLIGHT_BRIGHTNESS
 * ------------------------------------------------------------------------- */

bool mce_hybris_backlight_init            (void);
void mce_hybris_backlight_quit            (void);
bool mce_hybris_backlight_set_brightness  (int level);

/* ------------------------------------------------------------------------- *
 * KEYPAD_BACKLIGHT_BRIGHTNESS
 * ------------------------------------------------------------------------- */

bool mce_hybris_keypad_init               (void);
void mce_hybris_keypad_quit               (void);
bool mce_hybris_keypad_set_brightness     (int level);

/* ------------------------------------------------------------------------- *
 * INDICATOR_LED_PATTERN
 * ------------------------------------------------------------------------- */

bool mce_hybris_indicator_init            (void);
void mce_hybris_indicator_quit            (void);
bool mce_hybris_indicator_set_pattern     (int r, int g, int b, int ms_on, int ms_off);
bool mce_hybris_indicator_can_breathe     (void);
void mce_hybris_indicator_enable_breathing(bool enable);
bool mce_hybris_indicator_set_brightness  (int level);

/* ------------------------------------------------------------------------- *
 * PROXIMITY_SENSOR
 * ------------------------------------------------------------------------- */

bool mce_hybris_ps_init                   (void);
void mce_hybris_ps_quit                   (void);
bool mce_hybris_ps_set_active             (bool state);
void mce_hybris_ps_set_hook               (mce_hybris_ps_fn cb);

/* ------------------------------------------------------------------------- *
 * AMBIENT_LIGHT_SENSOR
 * ------------------------------------------------------------------------- */

bool mce_hybris_als_init                  (void);
void mce_hybris_als_quit                  (void);
bool mce_hybris_als_set_active            (bool state);
void mce_hybris_als_set_hook              (mce_hybris_als_fn cb);

/* ------------------------------------------------------------------------- *
 * GENERIC
 * ------------------------------------------------------------------------- */

void mce_hybris_quit                      (void);

/* ========================================================================= *
 * FRAME_BUFFER_POWER_STATE
 * ========================================================================= */

/** Initialize libhybris frame buffer device object
 *
 * @return true on success, false on failure
 */
bool
mce_hybris_framebuffer_init(void)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  return hybris_device_fb_init();
#else
  return false;
#endif
}

/** Release libhybris frame buffer device object
 */
void
mce_hybris_framebuffer_quit(void)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  hybris_device_fb_quit();
#endif
}

/** Set frame buffer power state via libhybris
 *
 * @param state true to power on, false to power off
 *
 * @return true on success, false on failure
 */
bool
mce_hybris_framebuffer_set_power(bool state)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  return hybris_device_fb_set_power(state);
#else
  (void)state;
  return false;
#endif
}

/* ========================================================================= *
 * DISPLAY_BACKLIGHT_BRIGHTNESS
 * ========================================================================= */

/** Initialize libhybris display backlight device object
 *
 * @return true on success, false on failure
 */
bool
mce_hybris_backlight_init(void)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  return hybris_device_backlight_init();
#else
  return false;
#endif
}

/** Release libhybris display backlight device object
 */
void
mce_hybris_backlight_quit(void)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  hybris_device_backlight_quit();
#endif
}

/** Set display backlight brightness via libhybris
 *
 * @param level 0=off ... 255=maximum brightness
 *
 * @return true on success, false on failure
 */
bool
mce_hybris_backlight_set_brightness(int level)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  return hybris_device_backlight_set_brightness(level);
#else
  (void)level;
  return false;
#endif
}

/* ========================================================================= *
 * KEYPAD_BACKLIGHT_BRIGHTNESS
 * ========================================================================= */

/** Initialize libhybris keypad backlight device object
 *
 * @return true on success, false on failure
 */
bool
mce_hybris_keypad_init(void)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  return hybris_device_keypad_init();
#else
  return false;
#endif
}

/** Release libhybris keypad backlight device object
 */
void
mce_hybris_keypad_quit(void)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  hybris_device_keypad_quit();
#endif
}

/** Set display keypad brightness via libhybris
 *
 * @param level 0=off ... 255=maximum brightness
 *
 * @return true on success, false on failure
 */
bool
mce_hybris_keypad_set_brightness(int level)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  return hybris_device_keypad_set_brightness(level);
#else
  (void)level;
  return false;
#endif
}

/* ========================================================================= *
 * INDICATOR_LED_PATTERN
 * ========================================================================= */

/** Clamp integer values to given range
 *
 * @param lo  minimum value allowed
 * @param hi  maximum value allowed
 * @param val value to clamp
 *
 * @return val clamped to [lo, hi]
 */
static inline int
clamp_to_range(int lo, int hi, int val)
{
  return val <= lo ? lo : val <= hi ? val : hi;
}

/** Flag for: controls for RGB leds exist in sysfs */
static bool mce_hybris_indicator_uses_sysfs = false;

/** Initialize libhybris indicator led device object
 *
 * @return true on success, false on failure
 */
bool
mce_hybris_indicator_init(void)
{
  static bool done = false;
  static bool ack  = false;

  if( done ) {
    goto cleanup;
  }

  done = true;

  if( sysfs_led_init() ) {
    mce_hybris_indicator_uses_sysfs = true;
  }
#ifdef ENABLE_HYBRIS_SUPPORT
  else if( !hybris_device_indicator_init() ) {
    goto  cleanup;
  }
#else
  else {
    goto cleanup;
  }
#endif

  ack = true;

cleanup:

  mce_log(LL_DEBUG, "res = %s", ack ? "true" : "false");

  return ack;
}

/** Release libhybris indicator led device object
 */
void
mce_hybris_indicator_quit(void)
{
  if( mce_hybris_indicator_uses_sysfs ) {
    /* Release sysfs controls */
    sysfs_led_quit();
  }
#ifdef ENABLE_HYBRIS_SUPPORT
  else {
    /* Release libhybris controls */
    hybris_device_indicator_quit();
  }
#endif
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
mce_hybris_indicator_set_pattern(int r, int g, int b, int ms_on, int ms_off)
{
  bool     ack = false;

  /* Sanitize input values */

  /* Clamp time periods to [0, 60] second range.
   *
   * While periods longer than few seconds might not count as "blinking",
   * we need to leave some slack to allow beacon style patterns with
   * relatively long off periods */
  ms_on  = clamp_to_range(0, 60000, ms_on);
  ms_off = clamp_to_range(0, 60000, ms_off);

  /* Both on and off periods need to be non-zero for the blinking
   * to happen in the first place. And if the periods are too
   * short it starts to look like led failure more than indication
   * of something. */
  if( ms_on < 50 || ms_off < 50 ) {
    ms_on = ms_off = 0;
  }

  /* Clamp rgb values to [0, 255] range */
  r = clamp_to_range(0, 255, r);
  g = clamp_to_range(0, 255, g);
  b = clamp_to_range(0, 255, b);

  /* Use raw sysfs controls if possible */

  if( mce_hybris_indicator_uses_sysfs ) {
    ack = sysfs_led_set_pattern(r, g, b, ms_on, ms_off);
  }
#ifdef ENABLE_HYBRIS_SUPPORT
  else {
    ack = hybris_device_indicator_set_pattern(r, g, b, ms_on, ms_off);
  }
#endif

  mce_log(LL_DEBUG, "pattern(%d,%d,%d,%d,%d) -> %s",
          r,g,b, ms_on, ms_off , ack ? "success" : "failure");

  return ack;
}

/** Query if currently active led backend can support breathing
 *
 * @return true if breathing can be requested, false otherwise
 */
bool
mce_hybris_indicator_can_breathe(void)
{
  bool ack = false;

  /* Note: We can't know how access via hybris behaves, so err
   *       on the safe side and assume that breathing is not ok
   *       unless we have direct sysfs controls.
   */

  if( mce_hybris_indicator_uses_sysfs )
  {
    ack = sysfs_led_can_breathe();
  }

  /* The result does not change during runtime of mce, so
   * log only once */

  static bool logged = false;

  if( !logged )
  {
    logged = true;
    mce_log(LL_DEBUG, "res = %s", ack ? "true" : "false");
  }

  return ack;
}

/** Enable/disable sw breathing
 *
 * @param enable true to enable sw breathing, false to disable
 */
void
mce_hybris_indicator_enable_breathing(bool enable)
{
  mce_log(LL_DEBUG, "enable = %s", enable ? "true" : "false");

  if( mce_hybris_indicator_uses_sysfs ) {
    sysfs_led_set_breathing(enable);
  }
}

/** Set indicator led brightness
 *
 * @param level 1=minimum, 255=maximum
 *
 * @return true on success, or false on failure
 */
bool
mce_hybris_indicator_set_brightness(int level)
{
  mce_log(LL_DEBUG, "level = %d", level);

  if( mce_hybris_indicator_uses_sysfs ) {
    /* Clamp brightness values to [1, 255] range */
    level = clamp_to_range(1, 255, level);

    sysfs_led_set_brightness(level);
  }

  /* Note: failure means this function is not available - which is
   * handled at mce side stub. From this plugin we always return true */
  return true;
}

/* ========================================================================= *
 * PROXIMITY_SENSOR
 * ========================================================================= */

/** Start using proximity sensor via libhybris
 *
 * @return true on success, false on failure
 */
bool
mce_hybris_ps_init(void)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  return hybris_sensor_ps_init();
#else
  return false;
#endif
}

/** Stop using proximity sensor via libhybris
 *
 * @return true on success, false on failure
 */
void
mce_hybris_ps_quit(void)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  hybris_sensor_ps_quit();
#endif
}

/** Set proximity sensort input enabled state
 *
 * @param state true to enable input, or false to disable input
 */
bool
mce_hybris_ps_set_active(bool state)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  return hybris_sensor_ps_set_active(state);
#else
  (void)state;
  return false;
#endif
}

/** Set callback function for handling proximity sensor events
 *
 * Note: the callback function will be called from worker thread.
 */
void
mce_hybris_ps_set_hook(mce_hybris_ps_fn cb)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  hybris_sensor_ps_set_hook(cb);
#else
  (void)cb;
#endif
}

/* ========================================================================= *
 * AMBIENT_LIGHT_SENSOR
 * ========================================================================= */

/** Start using ambient light sensor via libhybris
 *
 * @return true on success, false on failure
 */
bool
mce_hybris_als_init(void)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  return hybris_device_als_init();
#else
  return false;
#endif
}

/** Stop using ambient light sensor via libhybris
 *
 * @return true on success, false on failure
 */
void
mce_hybris_als_quit(void)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  hybris_device_als_quit();
#endif
}

/** Set ambient light sensor input enabled state
 *
 * @param state true to enable input, or false to disable input
 */
bool
mce_hybris_als_set_active(bool state)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  return hybris_device_als_set_active(state);
#else
  (void)state;
  return false;
#endif
}

/** Set callback function for handling ambient light sensor events
 *
 * Note: the callback function will be called from worker thread.
 */
void
mce_hybris_als_set_hook(mce_hybris_als_fn cb)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  hybris_device_als_set_hook(cb);
#else
  (void)cb;
#endif
}

/* ========================================================================= *
 * GENERIC
 * ========================================================================= */

/** Release all resources allocated by this module
 */
void
mce_hybris_quit(void)
{
#ifdef ENABLE_HYBRIS_SUPPORT
  hybris_plugin_fb_unload();
  hybris_plugin_lights_unload();
  hybris_plugin_sensors_unload();
#endif
}
