/** @file plugin-api.h
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

/* FIXME: This header is included in sourcetrees of both mce and
 *        mce-plugin-libhybris. For now it must be kept in sync
 *        manually.
 */

#ifndef  MCE_HYBRIS_H_
# define MCE_HYBRIS_H_

# ifndef MCE_HYBRIS_INTERNAL
#  define MCE_HYBRIS_INTERNAL 0
# endif

# include <stdint.h>
# include <stdbool.h>

# ifdef __cplusplus
extern "C" {
# elif 0
} /* fool JED indentation ... */
# endif

# pragma GCC visibility push(default)

/* - - - - - - - - - - - - - - - - - - - *
 * frame buffer power state
 * - - - - - - - - - - - - - - - - - - - */

bool mce_hybris_framebuffer_init(void);
void mce_hybris_framebuffer_quit(void);
bool mce_hybris_framebuffer_set_power(bool on);

/* - - - - - - - - - - - - - - - - - - - *
 * display backlight brightness
 * - - - - - - - - - - - - - - - - - - - */

bool mce_hybris_backlight_init(void);
void mce_hybris_backlight_quit(void);
bool mce_hybris_backlight_set_brightness(int level);

/* - - - - - - - - - - - - - - - - - - - *
 * keypad backlight brightness
 * - - - - - - - - - - - - - - - - - - - */

bool mce_hybris_keypad_set_brightness(int level);
void mce_hybris_keypad_quit(void);
bool mce_hybris_keypad_init(void);

/* - - - - - - - - - - - - - - - - - - - *
 * indicator led pattern
 * - - - - - - - - - - - - - - - - - - - */

bool mce_hybris_indicator_init(void);
void mce_hybris_indicator_quit(void);
bool mce_hybris_indicator_set_pattern(int r, int g, int b, int ms_on, int ms_off);
void mce_hybris_indicator_enable_breathing(bool enable);
bool mce_hybris_indicator_set_brightness(int level);
bool mce_hybris_indicator_can_breathe(void);

/* - - - - - - - - - - - - - - - - - - - *
 * proximity sensor
 * - - - - - - - - - - - - - - - - - - - */

typedef void (*mce_hybris_ps_fn)(int64_t timestamp, float distance);

bool mce_hybris_ps_init(void);
void mce_hybris_ps_quit(void);
bool mce_hybris_ps_set_active(bool active);
bool mce_hybris_ps_set_callback(mce_hybris_ps_fn cb);

/* - - - - - - - - - - - - - - - - - - - *
 * ambient light sensor
 * - - - - - - - - - - - - - - - - - - - */

typedef void (*mce_hybris_als_fn)(int64_t timestamp, float light);

bool mce_hybris_als_init(void);
void mce_hybris_als_quit(void);
bool mce_hybris_als_set_active(bool active);
bool mce_hybris_als_set_callback(mce_hybris_als_fn cb);

/* - - - - - - - - - - - - - - - - - - - *
 * generic
 * - - - - - - - - - - - - - - - - - - - */

void mce_hybris_quit(void);

/* - - - - - - - - - - - - - - - - - - - *
 * internal to module <--> plugin
 * - - - - - - - - - - - - - - - - - - - */

# if MCE_HYBRIS_INTERNAL >= 1
typedef void (*mce_hybris_log_fn)(int lev, const char *file, const char *func,
                                  const char *text);
# endif

# if MCE_HYBRIS_INTERNAL >= 2
void mce_hybris_set_log_hook(mce_hybris_log_fn cb);
void mce_hybris_ps_set_hook(mce_hybris_ps_fn cb);
void mce_hybris_als_set_hook(mce_hybris_als_fn cb);
# endif

# pragma GCC visibility pop

# ifdef __cplusplus
};
# endif

#endif /* MCE_HYBRIS_H_ */
