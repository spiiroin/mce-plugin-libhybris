/** @file sysfs-led-vanilla.c
 *
 * mce-plugin-libhybris - Libhybris plugin for Mode Control Entity
 * <p>
 * Copyright (C) 2013-2017 Jolla Ltd.
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
 * RGB led control: Jolla 1 backend
 *
 * Three channels, all of which:
 * - must have 'brightness' control file
 * - must have 'max_brightness' control file or nonzero fixed maximum
 * - can have either
 *    o blink on/off delay control files
 *    o blink enabled/disable control control file
 *
 * Assumptions built into code:
 *
 * - The write() calls to sysfs controls return immediately from kernel to
 *   userspace, but the kernel side can stay busy with the change for few
 *   milliseconds -> Frequent intensity changes do not block mce process,
 *   so sw breathing is feasible. Minimum delay between led state changes
 *   must be enforced.
 *
 * - Blink controls for the R, G and B channels are independent. To avoid
 *   "rainbow patterns" when more than one channel is used the blink enabling
 *   for all of the channels as simultaneously as possible.
 * ========================================================================= */

#include "sysfs-led-vanilla.h"
#include "sysfs-led-util.h"

#include <stdio.h>

#include <glib.h>

/* ========================================================================= *
 * PROTOTYPES
 * ========================================================================= */

typedef struct
{
  const char *max;   // R
  const char *val;   // W
  const char *on;    // W
  const char *off;   // W
  const char *blink; // W
  int         maxval;// value to use if max path is NULL
} led_paths_vanilla_t;

typedef struct
{
  int maxval;
  int fd_val;
  int fd_on;
  int fd_off;
  int fd_blink;

  int cur_val;
  int cur_on;
  int cur_off;
  int cur_blink;

} led_channel_vanilla_t;

/* ------------------------------------------------------------------------- *
 * ONE_CHANNEL
 * ------------------------------------------------------------------------- */

static void        led_channel_vanilla_init          (led_channel_vanilla_t *self);
static void        led_channel_vanilla_close         (led_channel_vanilla_t *self);
static bool        led_channel_vanilla_probe         (led_channel_vanilla_t *self, const led_paths_vanilla_t *path);
static void        led_channel_vanilla_set_value     (led_channel_vanilla_t *self, int value);
static void        led_channel_vanilla_set_blink     (led_channel_vanilla_t *self, int on_ms, int off_ms);

/* ------------------------------------------------------------------------- *
 * ALL_CHANNELS
 * ------------------------------------------------------------------------- */

static void        led_control_vanilla_blink_cb      (void *data, int on_ms, int off_ms);
static void        led_control_vanilla_value_cb      (void *data, int r, int g, int b);
static void        led_control_vanilla_close_cb      (void *data);

bool               led_control_vanilla_probe         (led_control_t *self);

/* ========================================================================= *
 * ONE_CHANNEL
 * ========================================================================= */

static void
led_channel_vanilla_init(led_channel_vanilla_t *self)
{
  self->fd_on     = -1;
  self->fd_off    = -1;
  self->fd_val    = -1;
  self->fd_blink  = -1;
  self->maxval    = -1;

  self->cur_val   = -1;
  self->cur_on    = -1;
  self->cur_off   = -1;
  self->cur_blink = -1;
}

static void
led_channel_vanilla_close(led_channel_vanilla_t *self)
{
  led_util_close_file(&self->fd_on);
  led_util_close_file(&self->fd_off);
  led_util_close_file(&self->fd_val);
  led_util_close_file(&self->fd_blink);
}

static bool
led_channel_vanilla_probe(led_channel_vanilla_t *self,
                          const led_paths_vanilla_t *path)
{
  bool res = false;

  led_channel_vanilla_close(self);

  // maximum brightness can be read from file or given in config
  if( path->max )
  {
    self->maxval = led_util_read_number(path->max);
  }
  else
  {
    self->maxval = path->maxval;
  }

  if( self->maxval <= 0 )
  {
    goto cleanup;
  }

  // we always must have brightness control
  if( !led_util_open_file(&self->fd_val, path->val) )
  {
    goto cleanup;
  }

  // on/off period controls are optional, but both
  // are needed if one is present
  if( led_util_open_file(&self->fd_on, path->on) )
  {
    if( !led_util_open_file(&self->fd_off, path->off) )
    {
      led_util_close_file(&self->fd_on);
    }
  }

  // having "blink" control file is optional
  led_util_open_file(&self->fd_blink, path->blink);

  res = true;

cleanup:

  if( !res ) led_channel_vanilla_close(self);

  return res;
}

static void
led_channel_vanilla_set_value(led_channel_vanilla_t *self,
                              int value)
{
  if( self->fd_val != -1 )
  {
    value = led_util_scale_value(value, self->maxval);
    if( self->cur_val != value )
    {
      self->cur_val   = value;
      self->cur_blink = -1;
      dprintf(self->fd_val, "%d", value);
    }
  }

  if( self->fd_blink != -1 )
  {
    int blink = (self->cur_on > 0 && self->cur_off > 0);
    if( self->cur_blink != blink )
    {
      self->cur_blink = blink;
      dprintf(self->fd_blink, "%d", blink);
    }
  }
}
static void
led_channel_vanilla_set_blink(led_channel_vanilla_t *self,
                              int on_ms, int off_ms)
{
  /* Note: Blinking config is taken in use when brightness
   *       sysfs is written to -> we need to invalidate
   *       cached brightness value if blinking changes
   *       are made.
   */

  if( self->fd_on != -1 && self->cur_on != on_ms )
  {
    self->cur_on    = on_ms;
    self->cur_val   = -1;
    self->cur_blink = -1;
    dprintf(self->fd_on,  "%d", on_ms);
  }

  if( self->fd_off != -1 && self->cur_off != off_ms )
  {
    self->cur_off   = off_ms;
    self->cur_val   = -1;
    self->cur_blink = -1;
    dprintf(self->fd_off, "%d", off_ms);
  }
}

/* ========================================================================= *
 * ALL_CHANNELS
 * ========================================================================= */

static void
led_control_vanilla_blink_cb(void *data, int on_ms, int off_ms)
{
  led_channel_vanilla_t *channel = data;
  led_channel_vanilla_set_blink(channel + 0, on_ms, off_ms);
  led_channel_vanilla_set_blink(channel + 1, on_ms, off_ms);
  led_channel_vanilla_set_blink(channel + 2, on_ms, off_ms);
}

static void
led_control_vanilla_value_cb(void *data, int r, int g, int b)
{
  led_channel_vanilla_t *channel = data;
  led_channel_vanilla_set_value(channel + 0, r);
  led_channel_vanilla_set_value(channel + 1, g);
  led_channel_vanilla_set_value(channel + 2, b);
}

static void
led_control_vanilla_close_cb(void *data)
{
  led_channel_vanilla_t *channel = data;
  led_channel_vanilla_close(channel + 0);
  led_channel_vanilla_close(channel + 1);
  led_channel_vanilla_close(channel + 2);
}

bool
led_control_vanilla_probe(led_control_t *self)
{
  /** Sysfs control paths for RGB leds */
  static const led_paths_vanilla_t paths[][3] =
  {
    // vanilla
    {
      {
        .on  = "/sys/class/leds/led:rgb_red/blink_delay_on",
        .off = "/sys/class/leds/led:rgb_red/blink_delay_off",
        .val = "/sys/class/leds/led:rgb_red/brightness",
        .max = "/sys/class/leds/led:rgb_red/max_brightness",
      },
      {
        .on  = "/sys/class/leds/led:rgb_green/blink_delay_on",
        .off = "/sys/class/leds/led:rgb_green/blink_delay_off",
        .val = "/sys/class/leds/led:rgb_green/brightness",
        .max = "/sys/class/leds/led:rgb_green/max_brightness",
      },
      {
        .on  = "/sys/class/leds/led:rgb_blue/blink_delay_on",
        .off = "/sys/class/leds/led:rgb_blue/blink_delay_off",
        .val = "/sys/class/leds/led:rgb_blue/brightness",
        .max = "/sys/class/leds/led:rgb_blue/max_brightness",
      }
    },
    // i9300 (galaxy s3 international)
    {
      {
        .on    = "/sys/class/leds/led_r/delay_on",
        .off   = "/sys/class/leds/led_r/delay_off",
        .val   = "/sys/class/leds/led_r/brightness",
        .max   = "/sys/class/leds/led_r/max_brightness",
        .blink = "/sys/class/leds/led_r/blink",
      },
      {
        .on    = "/sys/class/leds/led_g/delay_on",
        .off   = "/sys/class/leds/led_g/delay_off",
        .val   = "/sys/class/leds/led_g/brightness",
        .max   = "/sys/class/leds/led_g/max_brightness",
        .blink = "/sys/class/leds/led_g/blink",
      },
      {
        .on    = "/sys/class/leds/led_b/delay_on",
        .off   = "/sys/class/leds/led_b/delay_off",
        .val   = "/sys/class/leds/led_b/brightness",
        .max   = "/sys/class/leds/led_b/max_brightness",
        .blink = "/sys/class/leds/led_b/blink",
      }
    },
    // yuga (sony xperia z)
    {
      {
        .val    = "/sys/class/leds/lm3533-red/brightness",
        .maxval = 255,
      },
      {
        .val    = "/sys/class/leds/lm3533-green/brightness",
        .maxval = 255,
      },
      {
        .val    = "/sys/class/leds/lm3533-blue/brightness",
        .maxval = 255,
      },
    },
    // onyx (OnePlus X)
    {
      {
        .val    = "/sys/class/leds/red/brightness",
        .max    = "/sys/class/leds/red/max_brightness",
        .on     = "/sys/class/leds/red/pause_hi",
        .off    = "/sys/class/leds/red/pause_lo",
        .blink  = "/sys/class/leds/red/blink",
      },
      {
        .val    = "/sys/class/leds/green/brightness",
        .max    = "/sys/class/leds/green/max_brightness",
        .on     = "/sys/class/leds/green/pause_hi",
        .off    = "/sys/class/leds/green/pause_lo",
        .blink  = "/sys/class/leds/green/blink",
      },
      {
        .val    = "/sys/class/leds/blue/brightness",
        .max    = "/sys/class/leds/blue/max_brightness",
        .on     = "/sys/class/leds/blue/pause_hi",
        .off    = "/sys/class/leds/blue/pause_lo",
        .blink  = "/sys/class/leds/blue/blink",
      },
    },
  };

  static led_channel_vanilla_t channel[3];

  bool res = false;

  led_channel_vanilla_init(channel+0);
  led_channel_vanilla_init(channel+1);
  led_channel_vanilla_init(channel+2);

  self->name   = "vanilla";
  self->data   = channel;
  self->enable = 0;
  self->blink  = led_control_vanilla_blink_cb;
  self->value  = led_control_vanilla_value_cb;
  self->close  = led_control_vanilla_close_cb;

  for( size_t i = 0; i < G_N_ELEMENTS(paths) ; ++i )
  {
    if( led_channel_vanilla_probe(&channel[0], &paths[i][0]) &&
        led_channel_vanilla_probe(&channel[1], &paths[i][1]) &&
        led_channel_vanilla_probe(&channel[2], &paths[i][2]) )
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
