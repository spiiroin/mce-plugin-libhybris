/** @file sysfs-led-bacon.c
 *
 * mce-plugin-libhybris - Libhybris plugin for Mode Control Entity
 * <p>
 * Copyright (C) 2017 Jolla Ltd.
 * <p>
 * @Author Willem-Jan de Hoog <wdehoog@exalondelft.nl>
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
 * RGB led control: bacon backend
 *
 * Three channels, all of which:
 * - must have 'brightness' control file
 * - must have 'grpfreq', 'grppwm' and 'blink' to blink delay control file
 * - must have 'reset' control file
 *
 * Based on code from device/oneplus/bacon/liblight/lights.c
 * ========================================================================= */

#include "sysfs-led-bacon.h"

#include "sysfs-led-util.h"
#include "plugin-logging.h"

#include <stdio.h>

#include <glib.h>

/* ========================================================================= *
 * PROTOTYPES
 * ========================================================================= */

typedef struct
{
  const char *brightness;
  const char *grpfreq;
  const char *grppwm;
  const char *blink;
  const char *ledreset;
} led_paths_bacon_t;

typedef struct
{
  int fd_brightness;
  int fd_grpfreq;
  int fd_grppwm;
  int fd_blink;
  int fd_ledreset;

  int brightness;
  int freq;
  int pwm;
  int blink;
  int maxval;
} led_channel_bacon_t;

/* ------------------------------------------------------------------------- *
 * ONE_CHANNEL
 * ------------------------------------------------------------------------- */

static void        led_channel_bacon_init            (led_channel_bacon_t *self);
static void        led_channel_bacon_close           (led_channel_bacon_t *self);
static bool        led_channel_bacon_probe           (led_channel_bacon_t *self, const led_paths_bacon_t *path);

/* ------------------------------------------------------------------------- *
 * ALL_CHANNELS
 * ------------------------------------------------------------------------- */

static void        led_control_bacon_enable_cb       (void *data, bool enable);
static void        led_control_bacon_blink_cb        (void *data, int on_ms, int off_ms);
static void        led_control_bacon_value_cb        (void *data, int r, int g, int b);
static void        led_control_bacon_close_cb        (void *data);

bool               led_control_bacon_probe           (led_control_t *self);

/* ========================================================================= *
 * ONE_CHANNEL
 * ========================================================================= */

static void
led_channel_bacon_init(led_channel_bacon_t *self)
{
  self->fd_brightness = -1;
  self->fd_grpfreq = -1;
  self->fd_grppwm = -1;
  self->fd_blink = -1;
  self->fd_ledreset = -1;

  self->blink = 0;
  self->maxval = 255; // load from max_brightness?
}

static void
led_channel_bacon_close(led_channel_bacon_t *self)
{
  led_util_close_file(&self->fd_brightness);
  led_util_close_file(&self->fd_grpfreq);
  led_util_close_file(&self->fd_grppwm);
  led_util_close_file(&self->fd_blink);
  led_util_close_file(&self->fd_ledreset);
}

static bool
led_channel_bacon_probe(led_channel_bacon_t *self,
                        const led_paths_bacon_t *path)
{
  bool res = false;

  led_channel_bacon_close(self);

  if( !led_util_open_file(&self->fd_brightness, path->brightness) ||
      !led_util_open_file(&self->fd_grpfreq, path->grpfreq) ||
      !led_util_open_file(&self->fd_grppwm, path->grppwm) ||
      !led_util_open_file(&self->fd_blink, path->blink) ||
      !led_util_open_file(&self->fd_ledreset, path->ledreset))
  {
    goto cleanup;
  }

  res = true;

cleanup:

  if( !res ) led_channel_bacon_close(self);

  return res;
}

/* ========================================================================= *
 * ALL_CHANNELS
 * ========================================================================= */

static void
led_control_bacon_enable_cb(void *data, bool enable)
{
  const led_channel_bacon_t *channel = data;
  mce_log(LL_INFO, "led_control_bacon_enable_cb(%d)", enable);

  if(!enable)
    dprintf(channel->fd_ledreset, "%d", 1);
}

static void
led_control_bacon_blink_cb(void *data, int on_ms, int off_ms)
{
  led_channel_bacon_t *channel = data;
  mce_log(LL_INFO, "led_control_bacon_blink_cb(%d,%d)", on_ms, off_ms);

  if( on_ms > 0 && off_ms > 0 ) {
    int totalMS = on_ms + off_ms;

    // the LED appears to blink about once per second if freq is 20
    // 1000ms / 20 = 50
    channel->freq = totalMS / 50;

    // pwm specifies the ratio of ON versus OFF
    // pwm = 0 -> always off
    // pwm = 255 => always on
    channel->pwm = (on_ms * 255) / totalMS;

    // the low 4 bits are ignored, so round up if necessary
    if( channel->pwm > 0 && channel->pwm < 16 )
      channel->pwm = 16;

    channel->blink = 1;
  } else {
    channel->blink = 0;
    channel->freq = 0;
    channel->pwm = 0;
  }

  if( channel->blink ) {
    dprintf(channel->fd_grpfreq, "%d", channel->freq);
    dprintf(channel->fd_grppwm, "%d", channel->pwm);
  }
  dprintf(channel->fd_blink, "%d", channel->blink);
}

static void
led_control_bacon_value_cb(void *data, int r, int g, int b)
{
  led_channel_bacon_t *channel = data;

  mce_log(LL_INFO, "led_control_bacon_value_cb(%d,%d,%d), blink=%d", r, g, b, channel->blink);
  if( channel->blink )
    dprintf(channel->fd_ledreset, "%d", 0);

  (channel+0)->brightness = led_util_scale_value(r, (channel+0)->maxval);
  (channel+1)->brightness = led_util_scale_value(g, (channel+1)->maxval);
  (channel+2)->brightness = led_util_scale_value(b, (channel+2)->maxval);

  dprintf((channel+0)->fd_brightness, "%d", (channel+0)->brightness);
  dprintf((channel+1)->fd_brightness, "%d", (channel+1)->brightness);
  dprintf((channel+2)->fd_brightness, "%d", (channel+2)->brightness);

  if( channel->blink ) {
    // need to reset the blink when changing color (do we?)
    dprintf(channel->fd_grpfreq, "%d", channel->freq); // 1s
    dprintf(channel->fd_grppwm, "%d", channel->pwm); // 50%?
    dprintf(channel->fd_blink, "%d", 1);
  } else
    dprintf(channel->fd_blink, "%d", 0);
}

static void
led_control_bacon_close_cb(void *data)
{
  led_channel_bacon_t *channel = data;
  led_channel_bacon_close(channel + 0);
  led_channel_bacon_close(channel + 1);
  led_channel_bacon_close(channel + 2);
}

bool
led_control_bacon_probe(led_control_t *self)
{
  /** Sysfs control paths for RGB leds */
  static const led_paths_bacon_t paths[][3] =
  {
    // bacon
    {
      {
        .brightness = "/sys/class/leds/red/brightness",
        .grpfreq    = "/sys/class/leds/red/device/grpfreq",
        .grppwm     = "/sys/class/leds/red/device/grppwm",
        .blink      = "/sys/class/leds/red/device/blink",
        .ledreset   = "/sys/class/leds/red/device/ledreset",
      },
      {
        .brightness = "/sys/class/leds/green/brightness",
        .grpfreq    = "/sys/class/leds/green/device/grpfreq",
        .grppwm     = "/sys/class/leds/green/device/grppwm",
        .blink      = "/sys/class/leds/green/device/blink",
        .ledreset   = "/sys/class/leds/green/device/ledreset",
      },
      {
        .brightness = "/sys/class/leds/blue/brightness",
        .grpfreq    = "/sys/class/leds/blue/device/grpfreq",
        .grppwm     = "/sys/class/leds/blue/device/grppwm",
        .blink      = "/sys/class/leds/blue/device/blink",
        .ledreset   = "/sys/class/leds/blue/device/ledreset",
      }
    },
  };

  static led_channel_bacon_t channel[3];

  bool res = false;

  led_channel_bacon_init(channel+0);
  led_channel_bacon_init(channel+1);
  led_channel_bacon_init(channel+2);

  self->name   = "bacon";
  self->data   = channel;
  self->enable = led_control_bacon_enable_cb;
  self->blink  = led_control_bacon_blink_cb;
  self->value  = led_control_bacon_value_cb;
  self->close  = led_control_bacon_close_cb;

  // need to check
  self->can_breathe = false;

  for( size_t i = 0; i < G_N_ELEMENTS(paths) ; ++i )
  {
    if( led_channel_bacon_probe(&channel[0], &paths[i][0]) &&
        led_channel_bacon_probe(&channel[1], &paths[i][1]) &&
        led_channel_bacon_probe(&channel[2], &paths[i][2]) )
    {
      mce_log(LL_INFO, "bacon probed!");
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
