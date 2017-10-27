/** @file sysfs-led-hammerhead.c
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
 * RGB led control: Hammerhead backend
 *
 * Three channels, all of which:
 * - must have 'brightness' control file
 * - must have 'max_brightness' control file
 * - must have 'on_off_ms' blink delay control file
 * - must have 'rgb_start' enable/disable control file
 *
 * Assumptions built into code:
 * - Blinking is always soft, handled by kernel driver / hw.
 * - The sysfs writes will block until change is finished -> Intensity
 *   changes are slow. Breathing from userspace can't be used as it
 *   would constantly block mce mainloop.
 * ========================================================================= */

#include "sysfs-led-hammerhead.h"

#include "sysfs-led-util.h"
#include "plugin-config.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>

/* ========================================================================= *
 * PROTOTYPES
 * ========================================================================= */

typedef struct
{
  const char *max_brightness;
  const char *brightness;
  const char *on_off_ms;
  const char *rgb_start;
} led_paths_hammerhead_t;

typedef struct
{
  int cached_max_brightness;
  int fd_brightness;
  int fd_on_off_ms;
  int fd_rgb_start;
} led_channel_hammerhead_t;

/* ------------------------------------------------------------------------- *
 * ONE_CHANNEL
 * ------------------------------------------------------------------------- */

static void        led_channel_hammerhead_init       (led_channel_hammerhead_t *self);
static void        led_channel_hammerhead_close      (led_channel_hammerhead_t *self);
static bool        led_channel_hammerhead_probe      (led_channel_hammerhead_t *self, const led_paths_hammerhead_t *path);
static void        led_channel_hammerhead_set_enabled(const led_channel_hammerhead_t *self, bool enable);
static void        led_channel_hammerhead_set_value  (const led_channel_hammerhead_t *self, int value);
static void        led_channel_hammerhead_set_blink  (const led_channel_hammerhead_t *self, int on_ms, int off_ms);

/* ------------------------------------------------------------------------- *
 * ALL_CHANNELS
 * ------------------------------------------------------------------------- */

static void        led_control_hammerhead_enable_cb  (void *data, bool enable);
static void        led_control_hammerhead_blink_cb   (void *data, int on_ms, int off_ms);
static void        led_control_hammerhead_value_cb   (void *data, int r, int g, int b);
static void        led_control_hammerhead_close_cb   (void *data);

bool               led_control_hammerhead_probe      (led_control_t *self);

/* ========================================================================= *
 * ONE_CHANNEL
 * ========================================================================= */

static void
led_channel_hammerhead_init(led_channel_hammerhead_t *self)
{
  self->cached_max_brightness = -1;
  self->fd_brightness         = -1;
  self->fd_on_off_ms          = -1;
  self->fd_rgb_start          = -1;
}

static void
led_channel_hammerhead_close(led_channel_hammerhead_t *self)
{
  led_util_close_file(&self->fd_brightness);
  led_util_close_file(&self->fd_on_off_ms);
  led_util_close_file(&self->fd_rgb_start);
}

static bool
led_channel_hammerhead_probe(led_channel_hammerhead_t *self,
                             const led_paths_hammerhead_t *path)
{
  bool res = false;

  led_channel_hammerhead_close(self);

  if( (self->cached_max_brightness = led_util_read_number(path->max_brightness)) <= 0 )
  {
    goto cleanup;
  }

  if( !led_util_open_file(&self->fd_brightness,    path->brightness)    ||
      !led_util_open_file(&self->fd_on_off_ms, path->on_off_ms) ||
      !led_util_open_file(&self->fd_rgb_start, path->rgb_start) )
  {
    goto cleanup;
  }

  res = true;

cleanup:

  if( !res ) led_channel_hammerhead_close(self);

  return res;
}

static void
led_channel_hammerhead_set_enabled(const led_channel_hammerhead_t *self,
                                   bool enable)
{
  if( self->fd_rgb_start != -1 )
  {
    dprintf(self->fd_rgb_start, "%d", enable);
  }
}

static void
led_channel_hammerhead_set_value(const led_channel_hammerhead_t *self,
                                 int value)
{
  if( self->fd_brightness != -1 )
  {
    dprintf(self->fd_brightness, "%d", led_util_scale_value(value, self->cached_max_brightness));
  }
}

static void
led_channel_hammerhead_set_blink(const led_channel_hammerhead_t *self,
                                 int on_ms, int off_ms)
{
  if( self->fd_on_off_ms != -1 )
  {
    char tmp[32];
    int len = snprintf(tmp, sizeof tmp, "%d %d", on_ms, off_ms);
    if( len > 0 && len <= (int)sizeof tmp )
    {
      if( write(self->fd_on_off_ms, tmp, len) < 0 ) {
        // dontcare, keep compiler from complaining too
      }
    }
  }
}

/* ========================================================================= *
 * ALL_CHANNELS
 * ========================================================================= */

#define HAMMERHEAD_CHANNELS 3

static void
led_control_hammerhead_enable_cb(void *data, bool enable)
{
  const led_channel_hammerhead_t *channel = data;
  led_channel_hammerhead_set_enabled(channel + 0, enable);
  led_channel_hammerhead_set_enabled(channel + 1, enable);
  led_channel_hammerhead_set_enabled(channel + 2, enable);
}

static void
led_control_hammerhead_blink_cb(void *data, int on_ms, int off_ms)
{
  const led_channel_hammerhead_t *channel = data;
  led_channel_hammerhead_set_blink(channel + 0, on_ms, off_ms);
  led_channel_hammerhead_set_blink(channel + 1, on_ms, off_ms);
  led_channel_hammerhead_set_blink(channel + 2, on_ms, off_ms);
}

static void
led_control_hammerhead_value_cb(void *data, int r, int g, int b)
{
  const led_channel_hammerhead_t *channel = data;
  led_channel_hammerhead_set_value(channel + 0, r);
  led_channel_hammerhead_set_value(channel + 1, g);
  led_channel_hammerhead_set_value(channel + 2, b);
}

static void
led_control_hammerhead_close_cb(void *data)
{
  led_channel_hammerhead_t *channel = data;
  led_channel_hammerhead_close(channel + 0);
  led_channel_hammerhead_close(channel + 1);
  led_channel_hammerhead_close(channel + 2);
}

static bool
led_control_hammerhead_static_probe(led_channel_hammerhead_t *channel)
{
  /** Sysfs control paths for RGB leds */
  static const led_paths_hammerhead_t paths[][HAMMERHEAD_CHANNELS] =
  {
    // hammerhead (Nexus 5)
    {
      {
        .max_brightness = "/sys/class/leds/red/max_brightness",
        .brightness     = "/sys/class/leds/red/brightness",
        .on_off_ms      = "/sys/class/leds/red/on_off_ms",
        .rgb_start      = "/sys/class/leds/red/rgb_start",
      },
      {
        .max_brightness = "/sys/class/leds/green/max_brightness",
        .brightness     = "/sys/class/leds/green/brightness",
        .on_off_ms      = "/sys/class/leds/green/on_off_ms",
        .rgb_start      = "/sys/class/leds/green/rgb_start",
      },
      {
        .max_brightness = "/sys/class/leds/blue/max_brightness",
        .brightness     = "/sys/class/leds/blue/brightness",
        .on_off_ms      = "/sys/class/leds/blue/on_off_ms",
        .rgb_start      = "/sys/class/leds/blue/rgb_start",
      }
    },
  };

  bool ack = false;

  for( size_t i = 0; i < G_N_ELEMENTS(paths); ++i ) {
    if( led_channel_hammerhead_probe(&channel[0], &paths[i][0]) &&
        led_channel_hammerhead_probe(&channel[1], &paths[i][1]) &&
        led_channel_hammerhead_probe(&channel[2], &paths[i][2]) ) {
      ack = true;
      break;
    }
  }

  return ack;
}

static bool
led_control_hammerhead_dynamic_probe(led_channel_hammerhead_t *channel)
{
  /* See inifiles/60-hammerhead.ini for example */
  static const objconf_t hammerhead_conf[] =
  {
    OBJCONF_FILE(led_paths_hammerhead_t, brightness,      Brightness),
    OBJCONF_FILE(led_paths_hammerhead_t, max_brightness,  MaxBrightness),
    OBJCONF_FILE(led_paths_hammerhead_t, on_off_ms,       OnOffMs),
    OBJCONF_FILE(led_paths_hammerhead_t, rgb_start,       RgbStart),
    OBJCONF_STOP
  };

  static const char * const pfix[HAMMERHEAD_CHANNELS] =
  {
    "Red", "Green", "Blue"
  };

  bool ack = false;

  led_paths_hammerhead_t paths[HAMMERHEAD_CHANNELS];

  memset(paths, 0, sizeof paths);
  for( size_t i = 0; i < HAMMERHEAD_CHANNELS; ++i )
    objconf_init(hammerhead_conf, &paths[i]);

  for( size_t i = 0; i < HAMMERHEAD_CHANNELS; ++i )
  {
    if( !objconf_parse(hammerhead_conf, &paths[i], pfix[i]) )
      goto cleanup;

    if( !led_channel_hammerhead_probe(channel+i, &paths[i]) )
      goto cleanup;
  }

  ack = true;

cleanup:

  for( size_t i = 0; i < HAMMERHEAD_CHANNELS; ++i )
    objconf_quit(hammerhead_conf, &paths[i]);

  return ack;
}

bool
led_control_hammerhead_probe(led_control_t *self)
{

  static led_channel_hammerhead_t channel[HAMMERHEAD_CHANNELS];

  bool ack = false;

  led_channel_hammerhead_init(channel+0);
  led_channel_hammerhead_init(channel+1);
  led_channel_hammerhead_init(channel+2);

  self->name   = "hammerhead";
  self->data   = channel;
  self->enable = led_control_hammerhead_enable_cb;
  self->blink  = led_control_hammerhead_blink_cb;
  self->value  = led_control_hammerhead_value_cb;
  self->close  = led_control_hammerhead_close_cb;

  /* Changing led parameters is so slow and consumes so much
   * cpu cycles that we just can't have breathing available */
  self->can_breathe = false;

  if( self->use_config )
    ack = led_control_hammerhead_dynamic_probe(channel);

  if( !ack )
    ack = led_control_hammerhead_static_probe(channel);

  if( !ack )
    led_control_close(self);

  return ack;
}
