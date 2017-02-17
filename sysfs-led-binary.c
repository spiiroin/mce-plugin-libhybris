/** @file sysfs-led-binary.c
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
 * Binary led control: Jolla C backend
 *
 * One channel, which:
 * - must have 'brightness' control file
 *
 * Assumptions built into code:
 * - Using zero brightness disables led, any non-zero value enables it -> If
 *   mce requests "black" rgb value, use brightness of zero - otherwise 255.
 * ========================================================================= */

#include "sysfs-led-binary.h"

#include "sysfs-led-util.h"

#include <stdio.h>

#include <glib.h>

/* ========================================================================= *
 * PROTOTYPES
 * ========================================================================= */

typedef struct
{
  const char *val;    // W
} led_paths_binary_t;

typedef struct
{
  int maxval;
  int fd_val;
  int val_last;
} led_channel_binary_t;

/* ------------------------------------------------------------------------- *
 * ONE_CHANNEL
 * ------------------------------------------------------------------------- */

static void        led_channel_binary_init           (led_channel_binary_t *self);
static void        led_channel_binary_close          (led_channel_binary_t *self);
static bool        led_channel_binary_probe          (led_channel_binary_t *self, const led_paths_binary_t *path);
static void        led_channel_binary_set_value      (led_channel_binary_t *self, int value);

/* ------------------------------------------------------------------------- *
 * ALL_CHANNELS
 * ------------------------------------------------------------------------- */

static void        led_control_binary_map_color      (int r, int g, int b, int *mono);
static void        led_control_binary_value_cb       (void *data, int r, int g, int b);
static void        led_control_binary_close_cb       (void *data);

bool               led_control_binary_probe          (led_control_t *self);

/* ========================================================================= *
 * ONE_CHANNEL
 * ========================================================================= */

static void
led_channel_binary_init(led_channel_binary_t *self)
{
  self->maxval   = -1;
  self->fd_val   = -1;
  self->val_last = -1;
}

static void
led_channel_binary_close(led_channel_binary_t *self)
{
  led_util_close_file(&self->fd_val);
}

static bool
led_channel_binary_probe(led_channel_binary_t *self,
                         const led_paths_binary_t *path)
{
  bool res = false;

  led_channel_binary_close(self);

  self->maxval = 1;

  if( !led_util_open_file(&self->fd_val,   path->val) )
  {
    goto cleanup;
  }

  res = true;

cleanup:

  if( !res ) led_channel_binary_close(self);

  return res;
}

static void
led_channel_binary_set_value(led_channel_binary_t *self,
                             int value)
{
  if( self->fd_val != -1 )
  {
    int scaled = led_util_scale_value(value, self->maxval);
    if( self->val_last != scaled ) {
      self->val_last = scaled;
      dprintf(self->fd_val, "%d", scaled);
    }
  }
}

/* ========================================================================= *
 * ALL_CHANNELS
 * ========================================================================= */

static void
led_control_binary_map_color(int r, int g, int b, int *mono)
{
  /* Only binary on/off control is available, use
   * 255 as logical level if nonzero rgb value has
   * been requested.
   */
  *mono = (r || g || b) ? 255 : 0;
}

static void
led_control_binary_value_cb(void *data, int r, int g, int b)
{
  led_channel_binary_t *channel = data;

  int mono = 0;
  led_control_binary_map_color(r, g, b, &mono);
  led_channel_binary_set_value(channel + 0, mono);
}

static void
led_control_binary_close_cb(void *data)
{
  led_channel_binary_t *channel = data;
  led_channel_binary_close(channel + 0);
}

bool
led_control_binary_probe(led_control_t *self)
{
  /** Sysfs control paths for binary leds */
  static const led_paths_binary_t paths[][1] =
  {
    // binary
    {
      {
        .val = "/sys/class/leds/button-backlight/brightness",
      },
    },
  };

  static led_channel_binary_t channel[1];

  bool res = false;

  led_channel_binary_init(channel+0);

  self->name   = "binary";
  self->data   = channel;
  self->enable = 0;
  self->blink  = 0;
  self->value  = led_control_binary_value_cb;
  self->close  = led_control_binary_close_cb;

  /* We can use sw breathing logic to simulate hw blinking */
  self->can_breathe = true;
  self->breath_type = LED_RAMP_HARD_STEP;

  for( size_t i = 0; i < G_N_ELEMENTS(paths) ; ++i )
  {
    if( led_channel_binary_probe(&channel[0], &paths[i][0]) )
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
