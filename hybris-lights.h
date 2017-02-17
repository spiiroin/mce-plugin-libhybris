/** @file hybris-lights.h
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

#ifndef  HYBRIS_LIGHTS_H_
# define HYBRIS_LIGHTS_H_

# include <stdbool.h>

bool hybris_plugin_lights_load       (void);
void hybris_plugin_lights_unload     (void);

bool hybris_device_backlight_init           (void);
void hybris_device_backlight_quit           (void);
bool hybris_device_backlight_set_brightness (int level);

bool hybris_device_keypad_init              (void);
void hybris_device_keypad_quit              (void);
bool hybris_device_keypad_set_brightness    (int level);

bool hybris_device_indicator_init           (void);
void hybris_device_indicator_quit           (void);
bool hybris_device_indicator_set_pattern    (int r, int g, int b, int ms_on, int ms_off);

#endif /* HYBRIS_LIGHTS_H_ */
