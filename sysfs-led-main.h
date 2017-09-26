/** @file sysfs-led-main.h
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

#ifndef  SYSFS_LED_MAIN_H_
# define SYSFS_LED_MAIN_H_

# include <stdbool.h>

/* ------------------------------------------------------------------------- *
 * LED_CONTROL - Common RGB LED control API
 * ------------------------------------------------------------------------- */

/** Brightness ramp type used for SW-breathing
 */
typedef enum {
  /** Used when sw breathing is not used */
  LED_RAMP_DISABLED  = 0,

  /** The default half sine curve */
  LED_RAMP_HALF_SINE = 1,

  /** Step function used for emulating blinking via sw breathing */
  LED_RAMP_HARD_STEP = 2,
} led_ramp_t;

typedef struct led_control_t led_control_t;

/** Led control backend
 */
struct led_control_t
{
  const char *name;
  void       *data;
  bool        can_breathe;
  bool        use_config;
  led_ramp_t  breath_type;
  void      (*enable)(void *data, bool enable);
  void      (*blink) (void *data, int on_ms, int off_ms);
  void      (*value) (void *data, int r, int g, int b);
  void      (*close) (void *data);
};

bool sysfs_led_init           (void);
void sysfs_led_quit           (void);
bool sysfs_led_set_pattern    (int r, int g, int b, int ms_on, int ms_off);
bool sysfs_led_can_breathe    (void);
void sysfs_led_set_breathing  (bool enable);
void sysfs_led_set_brightness (int level);

void led_control_close        (led_control_t *self);

#endif /* SYSFS_LED_MAIN_H_ */
