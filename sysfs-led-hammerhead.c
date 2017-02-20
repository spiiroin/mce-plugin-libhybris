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

#include <stdio.h>
#include <unistd.h>

#include <glib.h>

/* ========================================================================= *
 * PROTOTYPES
 * ========================================================================= */

typedef struct
{
  const char *max;    // R
  const char *val;    // W
  const char *on_off; // W
  const char *enable; // W
} led_paths_hammerhead_t;

typedef struct
{
  int maxval;
  int fd_val;
  int fd_on_off;
  int fd_enable;
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
  self->maxval    = -1;
  self->fd_val    = -1;
  self->fd_on_off = -1;
  self->fd_enable = -1;
}

static void
led_channel_hammerhead_close(led_channel_hammerhead_t *self)
{
  led_util_close_file(&self->fd_val);
  led_util_close_file(&self->fd_on_off);
  led_util_close_file(&self->fd_enable);
}

static bool
led_channel_hammerhead_probe(led_channel_hammerhead_t *self,
                             const led_paths_hammerhead_t *path)
{
  bool res = false;

  led_channel_hammerhead_close(self);

  if( (self->maxval = led_util_read_number(path->max)) <= 0 )
  {
    goto cleanup;
  }

  if( !led_util_open_file(&self->fd_val,    path->val)    ||
      !led_util_open_file(&self->fd_on_off, path->on_off) ||
      !led_util_open_file(&self->fd_enable, path->enable) )
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
  if( self->fd_enable != -1 )
  {
    dprintf(self->fd_enable, "%d", enable);
  }
}

static void
led_channel_hammerhead_set_value(const led_channel_hammerhead_t *self,
                                 int value)
{
  if( self->fd_val != -1 )
  {
    dprintf(self->fd_val, "%d", led_util_scale_value(value, self->maxval));
  }
}

static void
led_channel_hammerhead_set_blink(const led_channel_hammerhead_t *self,
                                 int on_ms, int off_ms)
{
  if( self->fd_on_off != -1 )
  {
    char tmp[32];
    int len = snprintf(tmp, sizeof tmp, "%d %d", on_ms, off_ms);
    if( len > 0 && len <= (int)sizeof tmp )
    {
      if( write(self->fd_on_off, tmp, len) < 0 ) {
        // dontcare, keep compiler from complaining too
      }
    }
  }
}

/* ========================================================================= *
 * ALL_CHANNELS
 * ========================================================================= */

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

bool
led_control_hammerhead_probe(led_control_t *self)
{
  /** Sysfs control paths for RGB leds */
  static const led_paths_hammerhead_t paths[][3] =
  {
    // hammerhead (Nexus 5)
    {
      {
        .max    = "/sys/class/leds/red/max_brightness",
        .val    = "/sys/class/leds/red/brightness",
        .on_off = "/sys/class/leds/red/on_off_ms",
        .enable = "/sys/class/leds/red/rgb_start",
      },
      {
        .max    = "/sys/class/leds/green/max_brightness",
        .val    = "/sys/class/leds/green/brightness",
        .on_off = "/sys/class/leds/green/on_off_ms",
        .enable = "/sys/class/leds/green/rgb_start",
      },
      {
        .max    = "/sys/class/leds/blue/max_brightness",
        .val    = "/sys/class/leds/blue/brightness",
        .on_off = "/sys/class/leds/blue/on_off_ms",
        .enable = "/sys/class/leds/blue/rgb_start",
      }
    },
  };

  static led_channel_hammerhead_t channel[3];

  bool res = false;

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

  for( size_t i = 0; i < G_N_ELEMENTS(paths) ; ++i )
  {
    if( led_channel_hammerhead_probe(&channel[0], &paths[i][0]) &&
        led_channel_hammerhead_probe(&channel[1], &paths[i][1]) &&
        led_channel_hammerhead_probe(&channel[2], &paths[i][2]) )
    {
      res = true;
      break;
    }
  }

  if( !res )
  {
    led_control_close(self);
  }

  return res;
}
