/** @file hybris-sensors.h
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

#ifndef  HYBRIS_SENSORS_H_
# define HYBRIS_SENSORS_H_

# include "plugin-api.h"

bool hybris_plugin_sensors_load    (void);
void hybris_plugin_sensors_unload  (void);

bool hybris_sensor_ps_init        (void);
void hybris_sensor_ps_quit        (void);
bool hybris_sensor_ps_set_active  (bool state);
void hybris_sensor_ps_set_hook    (mce_hybris_ps_fn cb);

bool hybris_device_als_init       (void);
void hybris_device_als_quit       (void);
bool hybris_device_als_set_active (bool state);
void hybris_device_als_set_hook   (mce_hybris_als_fn cb);

#endif /* HYBRIS_SENSORS_H_ */
