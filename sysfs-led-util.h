/** @file sysfs-led-util.h
 *
 * mce-plugin-libhybris - Libhybris plugin for Mode Control Entity
 * <p>
 * Copyright (c) 2013 - 2017 Jolla Ltd.
 * Copyright (c) 2024 Jollyboys Ltd.
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

/* ========================================================================= *
 * UTILITY
 * ========================================================================= */

/** Clamp float value to given range */
static inline float led_util_fclamp(float v, float l, float h)
{
    return v < l ? l : v < h ? v : h;
}

/** Translate float value in range1 to range2 */
static inline float led_util_ftrans(float v, float l1, float h1, float l2, float h2)
{
    return led_util_fclamp(l2 + (h2 - l2) * (v - l1) / (h1 - l1), l2, h2);
}

/** Maximum of two integer values */
static inline int led_util_max(int a, int b)
{
    return a > b ? a : b;
}

/** Maximum of three integer values */
static inline int led_util_max3(int a, int b, int c)
{
    return led_util_max(led_util_max(a, b), c);
}

/** Clamp integer value to given range */
static inline int led_util_clamp(int v, int l, int h)
{
    return v < l ? l : v < h ? v : h;
};

/** Translate integer value in range1 to range2 */
static inline int led_util_trans(int v, int l1, int h1, int l2, int h2)
{
    int d1 = h1 - l1;
    int d2 = h2 - l2;
    return led_util_clamp(l2 + (d2 * (v - l1) + d1 / 2) / d1, l2, h2);
}

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

int  led_util_read_number (const char *path);
void led_util_close_file  (int *fd_ptr);
bool led_util_open_file   (int *fd_ptr, const char *path);
int  led_util_scale_value (int in, int max);
int  led_util_gcd         (int a, int b);
int  led_util_roundup     (int val, int range);

#endif /* SYSFS_LED_UTIL_H_ */
