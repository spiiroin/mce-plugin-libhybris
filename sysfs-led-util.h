/** @file sysfs-led-util.h
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

#ifndef  SYSFS_LED_UTIL_H_
# define SYSFS_LED_UTIL_H_

# include <stdbool.h>

int  led_util_read_number (const char *path);
void led_util_close_file  (int *fd_ptr);
bool led_util_open_file   (int *fd_ptr, const char *path);
int  led_util_scale_value (int in, int max);
int  led_util_gcd         (int a, int b);
int  led_util_roundup     (int val, int range);

#endif /* SYSFS_LED_UTIL_H_ */
