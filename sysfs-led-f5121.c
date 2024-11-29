/** @file sysfs-led-f5121.c
 *
 * mce-plugin-libhybris - Libhybris plugin for Mode Control Entity
 * <p>
 * Copyright (C) 2017 Jolla Ltd.
 * <p>
 * @author Simo Piiroinen <simo.piiroinen@jollamobile.com>
 * @author Kimmo Lindholm <kimmo.lindholm@eke.fi>
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
 * RGB led control: Sony Xperia X backend
 *
 * Three channels, all of which:
 * - must have 'brightness' control file
 * - must have 'max_brightness' control file
 * - must have 'blink' control file
 *
 * Assumptions built into code:
 * - writing to 'blink' affects 'brightness' control too and vice versa
 * ========================================================================= */

#include "sysfs-led-f5121.h"
#include "sysfs-led-util.h"
#include "sysfs-val.h"
#include "plugin-config.h"

#include <stdio.h>
#include <string.h>

#include <glib.h>

/* ========================================================================= *
 * PROTOTYPES
 * ========================================================================= */

typedef struct
{
    const char *max_brightness;
    const char *brightness;
    const char *blink;
    const char *max_brightness_override;
} led_paths_f5121_t;

typedef struct
{
    sysfsval_t *cached_max_brightness;
    sysfsval_t *cached_brightness;
    sysfsval_t *cached_blink;

    int         control_value;
    bool        control_blink;

} led_channel_f5121_t;

/* ------------------------------------------------------------------------- *
 * ONE_CHANNEL
 * ------------------------------------------------------------------------- */

static void        led_channel_f5121_init          (led_channel_f5121_t *self);
static void        led_channel_f5121_close         (led_channel_f5121_t *self);
static bool        led_channel_f5121_probe         (led_channel_f5121_t *self, const led_paths_f5121_t *path);
static void        led_channel_f5121_set_value     (led_channel_f5121_t *self, int value);
static void        led_channel_f5121_set_blink     (led_channel_f5121_t *self, int on_ms, int off_ms);

/* ------------------------------------------------------------------------- *
 * ALL_CHANNELS
 * ------------------------------------------------------------------------- */

static void        led_control_f5121_blink_cb      (void *data, int on_ms, int off_ms);
static void        led_control_f5121_value_cb      (void *data, int r, int g, int b);
static void        led_control_f5121_close_cb      (void *data);

bool               led_control_f5121_probe         (led_control_t *self);

/* ========================================================================= *
 * ONE_CHANNEL
 * ========================================================================= */

static void
led_channel_f5121_init(led_channel_f5121_t *self)
{
    self->cached_max_brightness = sysfsval_create();
    self->cached_brightness     = sysfsval_create();
    self->cached_blink          = sysfsval_create();

    self->control_blink = false;
    self->control_value = 0;
}

static void
led_channel_f5121_close(led_channel_f5121_t *self)
{
    sysfsval_delete(self->cached_max_brightness),
        self->cached_max_brightness = 0;

    sysfsval_delete(self->cached_brightness),
        self->cached_brightness = 0;

    sysfsval_delete(self->cached_blink),
        self->cached_blink = 0;
}

static bool
led_channel_f5121_probe(led_channel_f5121_t *self,
                        const led_paths_f5121_t *path)
{
    bool ack = false;

    /* Probe control files in reverse existance likelihood order.
     * Practically all led control directories have 'brightness'
     * file, most have 'max_brightness' while only some have 'blink'.
     */
    if( !sysfsval_open_rw(self->cached_blink, path->blink) )
        goto cleanup;

    if( !sysfsval_open_rw(self->cached_max_brightness, path->max_brightness) )
        goto cleanup;

    /* If MaxBrightnessOverride has been configured, it will be used
     * instead of content of max_brightness file.
     */

    int max_brightness_override = 0;
    if( path->max_brightness_override )
        max_brightness_override = strtol(path->max_brightness_override, NULL, 0);
    if( max_brightness_override > 0 )
        sysfsval_set(self->cached_max_brightness, max_brightness_override);

    sysfsval_refresh(self->cached_max_brightness);

    mce_log(LOG_DEBUG, "%s: effective = %d", path->max_brightness,
            sysfsval_get(self->cached_max_brightness));

    if( sysfsval_get(self->cached_max_brightness) <= 0 )
        goto cleanup;

    if( !sysfsval_open_rw(self->cached_brightness, path->brightness) )
        goto cleanup;

    ack = true;

cleanup:

    /* Always close the max_brightness file */
    sysfsval_close(self->cached_max_brightness);

    /* On failure close the other files too */
    if( !ack ) {
        sysfsval_close(self->cached_brightness);
        sysfsval_close(self->cached_blink);
    }

    return ack;
}

static void
led_channel_f5121_set_value(led_channel_f5121_t *self,
                            int value)
{
    value = led_util_scale_value(value,
                                sysfsval_get(self->cached_max_brightness));

    /* Ignore blinking requests while brightness is zero. */
    if( value <= 0 )
        self->control_blink = false;

    /* Logically it probably should be that:
     * - writing blink=1 implies brightness=255
     * - writing blink=0 implies brightness=0
     * - writing brightness=n implies blink=0
     *
     * However it seems swithing between blinking and non-blinking
     * modes can cause hiccups that vary from one device to another
     * (stale sysfs values left behind, led stays off when it should
     * be lit, ...)
     *
     * So the logic is arranged that before switching from static
     * color to blinking, a brightness=0 is done before writing
     * blink=0, and when swithing from blinking to static color, a
     * blink=0 is done before writing brightness=0.
     *
     * Note that upper level state machine logic + caching of the
     * assumed sysfs values means that these transitions are done in
     * 3 steps (cancel previous mode, reset to black, switch to new
     * mode) with pproximately SYSFS_LED_KERNEL_DELAY ms in between
     * the steps.
     */
    if( self->control_blink ) {
        sysfsval_set(self->cached_brightness, 0);
        sysfsval_set(self->cached_blink, 1);
    }
    else {
        sysfsval_set(self->cached_blink, 0);
        sysfsval_set(self->cached_brightness, value);
    }

    return;
}
static void
led_channel_f5121_set_blink(led_channel_f5121_t *self,
                            int on_ms, int off_ms)
{
    /* The state machine at upper level adjusts blink setting first
     * followed by brighthess setting - in f5121 modifying one will
     * affect the other too and must thus be handled at the same
     * time -> just cache the requested state. */
    self->control_blink = (on_ms && off_ms);
}

/* ========================================================================= *
 * ALL_CHANNELS
 * ========================================================================= */

#define F5121_CHANNELS 3

static void
led_control_f5121_blink_cb(void *data, int on_ms, int off_ms)
{
    led_channel_f5121_t *channel = data;
    led_channel_f5121_set_blink(channel + 0, on_ms, off_ms);
    led_channel_f5121_set_blink(channel + 1, on_ms, off_ms);
    led_channel_f5121_set_blink(channel + 2, on_ms, off_ms);
}

static void
led_control_f5121_value_cb(void *data, int r, int g, int b)
{
    led_channel_f5121_t *channel = data;
    led_channel_f5121_set_value(channel + 0, r);
    led_channel_f5121_set_value(channel + 1, g);
    led_channel_f5121_set_value(channel + 2, b);
}

static void
led_control_f5121_close_cb(void *data)
{
    led_channel_f5121_t *channel = data;
    led_channel_f5121_close(channel + 0);
    led_channel_f5121_close(channel + 1);
    led_channel_f5121_close(channel + 2);
}

static bool
led_control_f5121_static_probe(led_channel_f5121_t *channel)
{
    /** Sysfs control paths for RGB leds */
    static const led_paths_f5121_t f5121_paths[][F5121_CHANNELS] =
    {
        // f5121 (Sony Xperia X)
        {
            {
                .max_brightness = "/sys/class/leds/led:rgb_red/max_brightness",
                .brightness     = "/sys/class/leds/led:rgb_red/brightness",
                .blink          = "/sys/class/leds/led:rgb_red/blink",
                .max_brightness_override = "255",
            },
            {
                .max_brightness = "/sys/class/leds/led:rgb_green/max_brightness",
                .brightness     = "/sys/class/leds/led:rgb_green/brightness",
                .blink          = "/sys/class/leds/led:rgb_green/blink",
                .max_brightness_override = "255",
            },
            {
                .max_brightness = "/sys/class/leds/led:rgb_blue/max_brightness",
                .brightness     = "/sys/class/leds/led:rgb_blue/brightness",
                .blink          = "/sys/class/leds/led:rgb_blue/blink",
                .max_brightness_override = "255",
            },
        },
        {
            {
                .max_brightness = "/sys/class/leds/red/max_brightness",
                .brightness     = "/sys/class/leds/red/brightness",
                .blink          = "/sys/class/leds/red/blink",
            },
            {
                .max_brightness = "/sys/class/leds/green/max_brightness",
                .brightness     = "/sys/class/leds/green/brightness",
                .blink          = "/sys/class/leds/green/blink",
            },
            {
                .max_brightness = "/sys/class/leds/blue/max_brightness",
                .brightness     = "/sys/class/leds/blue/brightness",
                .blink          = "/sys/class/leds/blue/blink",
            },
        },
    };

    bool ack = false;

    for( size_t i = 0; i < G_N_ELEMENTS(f5121_paths); ++i ) {
        if( led_channel_f5121_probe(&channel[0], &f5121_paths[i][0]) &&
            led_channel_f5121_probe(&channel[1], &f5121_paths[i][1]) &&
            led_channel_f5121_probe(&channel[2], &f5121_paths[i][2]) ) {
            ack = true;
            break;
        }
    }

    return ack;
}

static bool
led_control_f5121_dynamic_probe(led_channel_f5121_t *channel)
{
  /* See inifiles/60-f5121.ini for example */
  static const objconf_t f5121_conf[] =
  {
    OBJCONF_FILE(led_paths_f5121_t, brightness,      Brightness),
    OBJCONF_FILE(led_paths_f5121_t, max_brightness,  MaxBrightness),
    OBJCONF_FILE(led_paths_f5121_t, blink,           Blink),

    OBJCONF_STRING(led_paths_f5121_t, max_brightness_override, MaxBrightnessOverride, NULL),

    OBJCONF_STOP
  };

  static const char * const pfix[F5121_CHANNELS] =
  {
    "Red", "Green", "Blue"
  };

  bool ack = false;

  led_paths_f5121_t paths[F5121_CHANNELS];

  memset(paths, 0, sizeof paths);
  for( size_t i = 0; i < F5121_CHANNELS; ++i )
    objconf_init(f5121_conf, &paths[i]);

  for( size_t i = 0; i < F5121_CHANNELS; ++i )
  {
    if( !objconf_parse(f5121_conf, &paths[i], pfix[i]) )
      goto cleanup;

    if( !led_channel_f5121_probe(channel+i, &paths[i]) )
      goto cleanup;
  }

  ack = true;

cleanup:

  for( size_t i = 0; i < F5121_CHANNELS; ++i )
    objconf_quit(f5121_conf, &paths[i]);

  return ack;
}

bool
led_control_f5121_probe(led_control_t *self)
{
    static led_channel_f5121_t channel[F5121_CHANNELS];

    bool ack = false;

    led_channel_f5121_init(channel+0);
    led_channel_f5121_init(channel+1);
    led_channel_f5121_init(channel+2);

    self->name   = "f5121";
    self->data   = channel;
    self->enable = 0;
    self->blink  = led_control_f5121_blink_cb;
    self->value  = led_control_f5121_value_cb;
    self->close  = led_control_f5121_close_cb;

    /* Prefer to use the built-in soft-blinking */
    self->can_breathe = false;

    if( self->use_config )
        ack = led_control_f5121_dynamic_probe(channel);

    if( !ack )
        ack = led_control_f5121_static_probe(channel);

    if( !ack )
        led_control_close(self);

    return ack;
}
