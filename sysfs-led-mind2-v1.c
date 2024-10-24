/** @file sysfs-led-mind2-v1.h
 *
 * mce-plugin-libhybris - Libhybris plugin for Mode Control Entity
 * <p>
 * Copyright (c) 2024 Jollyboys Ltd.
 * <p>
 * @author Simo Piiroinen <simo.piiroinen@jolla.com>
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

#include "sysfs-led-mind2-v1.h"

#include "sysfs-led-util.h"
#include "sysfs-val.h"
#include "plugin-config.h"

#include <stdio.h>
#include <string.h>

#include <glib.h>

/* ========================================================================= *
 * Constants
 * ========================================================================= */

#define DIFFERENTIATE_OUTER_LED 0 /* Debugging: when nonzero, outer led is
                                   * made to use color that differs from
                                   * what mce has requested. */

enum {
    MIND2V1_LED_INNER,
    MIND2V1_LED_OUTER,
    MIND2V1_LED_COUNT
};

/* Cap brightness to [0, 15] range */
#define MIND2V1_MIN_BRIGHTNESS  0
#define MIND2V1_MAX_BRIGHTNESS 15

/* ========================================================================= *
 * Utility
 * ========================================================================= */

static inline int imax(int a, int b)
{
    return (a > b) ? a : b;
}

static inline int imax3(int a, int b, int c)
{
    return imax(imax(a, b), c);
}

static inline int icap(int v, int l, int h)
{
    return v < l ? l : v < h ? v : h;
};

/* ========================================================================= *
 * Types
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * LED_PATHS_MIND2V1
 * ------------------------------------------------------------------------- */

typedef struct {
    const char *brightness;
    const char *red;
    const char *green;
    const char *blue;
} led_paths_mind2v1_t;

/* ------------------------------------------------------------------------- *
 * LEDS_PATHS_MIND2V1
 * ------------------------------------------------------------------------- */

typedef struct {
    const char          *power;
    led_paths_mind2v1_t  led[MIND2V1_LED_COUNT];
} leds_paths_mind2v1_t;

/* ------------------------------------------------------------------------- *
 * LED_STATE_MIND2V1
 * ------------------------------------------------------------------------- */

typedef struct {
    sysfsval_t *cached_brightness; // 0 - N
    sysfsval_t *cached_red;        // 0 / 1
    sysfsval_t *cached_green;      // 0 / 1
    sysfsval_t *cached_blue;       // 0 / 1
} led_state_mind2v1_t;

/* ------------------------------------------------------------------------- *
 * LEDS_STATE_MIND2V1
 * ------------------------------------------------------------------------- */

typedef struct {
    sysfsval_t          *cached_power; // 0 / 1
    led_state_mind2v1_t  led[MIND2V1_LED_COUNT];
} leds_state_mind2v1_t;

/* ========================================================================= *
 * LED_STATE_MIND2V1
 * ========================================================================= */

static void
led_state_mind2v1_init(led_state_mind2v1_t *self)
{
    self->cached_brightness = sysfsval_create();
    self->cached_red        = sysfsval_create();
    self->cached_green      = sysfsval_create();
    self->cached_blue       = sysfsval_create();
}

static void
led_state_mind2v1_quit(led_state_mind2v1_t *self)
{
    sysfsval_delete_at(&self->cached_brightness);
    sysfsval_delete_at(&self->cached_red);
    sysfsval_delete_at(&self->cached_green);
    sysfsval_delete_at(&self->cached_blue);
}

static void
led_state_mind2v1_close(led_state_mind2v1_t *self)
{
    sysfsval_close(self->cached_brightness);
    sysfsval_close(self->cached_red);
    sysfsval_close(self->cached_green);
    sysfsval_close(self->cached_blue);
}

static bool
led_state_mind2v1_brobe(led_state_mind2v1_t *self, const led_paths_mind2v1_t *paths)
{
    bool res = false;

    if( !sysfsval_open_rw(self->cached_brightness, paths->brightness) )
        goto cleanup;

    if( !sysfsval_open_rw(self->cached_red, paths->red) )
        goto cleanup;

    if( !sysfsval_open_rw(self->cached_green, paths->green) )
        goto cleanup;

    if( !sysfsval_open_rw(self->cached_blue, paths->blue) )
        goto cleanup;

    res = true;

cleanup:
    if( !res )
        led_state_mind2v1_close(self);

    return res;
}

static void
led_state_mind2v1_set_value(led_state_mind2v1_t *self, int r, int g, int b)
{
    sysfsval_set(self->cached_red,   (r > 0) ? 1 : 0);
    sysfsval_set(self->cached_green, (g > 0) ? 1 : 0);
    sysfsval_set(self->cached_blue,  (b > 0) ? 1 : 0);

    int v = icap(imax3(r, g, b), MIND2V1_MIN_BRIGHTNESS, MIND2V1_MAX_BRIGHTNESS);
    sysfsval_set(self->cached_brightness, v);
}

static bool
led_state_mind2v1_is_active(led_state_mind2v1_t *self)
{
    return sysfsval_get(self->cached_brightness) > 0;
}

/* ========================================================================= *
 * LEDS_STATE_MIND2V1
 * ========================================================================= */

static bool
leds_state_mind2v1_valid_index(int idx)
{
    return 0 <= idx && idx < MIND2V1_LED_COUNT;
}

static void
leds_state_mind2v1_init(leds_state_mind2v1_t *self)
{
    self->cached_power = sysfsval_create();

    for( int idx = 0; idx < MIND2V1_LED_COUNT; ++idx )
        led_state_mind2v1_init(&self->led[idx]);
}

static void
leds_state_mind2v1_quit(leds_state_mind2v1_t *self)
{
    sysfsval_delete_at(&self->cached_power);

    for( int idx = 0; idx < MIND2V1_LED_COUNT; ++idx )
        led_state_mind2v1_quit(&self->led[idx]);
}

static void
leds_state_mind2v1_close(leds_state_mind2v1_t *self)
{
    sysfsval_close(self->cached_power);

    for( int idx = 0; idx < MIND2V1_LED_COUNT; ++idx )
        led_state_mind2v1_close(&self->led[idx]);
}

static bool
leds_state_mind2v1_probe(leds_state_mind2v1_t *self, const leds_paths_mind2v1_t *paths)
{
    bool res = false;

    if( !sysfsval_open_rw(self->cached_power, paths->power) )
        goto cleanup;

    for( int idx = 0; idx < MIND2V1_LED_COUNT; ++idx )
        if( !led_state_mind2v1_brobe(&self->led[idx], &paths->led[idx]) )
            goto cleanup;

    res = true;

cleanup:
    if( !res )
        leds_state_mind2v1_close(self);

    return res;
}

static void
leds_state_mind2v1_update_power(leds_state_mind2v1_t *self)
{
    bool power = false;

    for( int idx = 0; idx < MIND2V1_LED_COUNT; ++idx )
        if( (power = led_state_mind2v1_is_active(&self->led[idx])) )
            break;

    sysfsval_set(self->cached_power, power);
}

static void
leds_state_mind2v1_set_value(leds_state_mind2v1_t *self, int idx, int r, int g, int b)
{
    if( leds_state_mind2v1_valid_index(idx) )
        led_state_mind2v1_set_value(&self->led[idx], r, g, b);
}

/* ========================================================================= *
 * LED_CONTROL_MIND2V1
 * ========================================================================= */

static void
led_control_mind2v1_value_cb(void *data, int r, int g, int b)
{
    leds_state_mind2v1_t *state = data;

    leds_state_mind2v1_set_value(state, MIND2V1_LED_INNER, r, g, b);
#if DIFFERENTIATE_OUTER_LED
    leds_state_mind2v1_set_value(state, MIND2V1_LED_OUTER, g, b, r);
#else
    leds_state_mind2v1_set_value(state, MIND2V1_LED_OUTER, r, g, b);
#endif
    leds_state_mind2v1_update_power(state);
}

static void
led_control_mind2v1_close_cb(void *data)
{
    leds_state_mind2v1_t *state = data;

    leds_state_mind2v1_quit(state);
}

static bool
led_control_mind2v1_static_probe(leds_state_mind2v1_t *state)
{
    static const leds_paths_mind2v1_t paths = {
        .power = "/sys/class/leds/Power/brightness",
        .led   = {
            {
                .brightness = "/sys/class/leds/Irgb/brightness",
                .red        = "/sys/class/leds/Ired/brightness",
                .green      = "/sys/class/leds/Igreen/brightness",
                .blue       = "/sys/class/leds/Iblue/brightness",
            },
            {
                .brightness = "/sys/class/leds/Orgb/brightness",
                .red        = "/sys/class/leds/Ored/brightness",
                .green      = "/sys/class/leds/Ogreen/brightness",
                .blue       = "/sys/class/leds/Oblue/brightness",
            }
        }
    };

    return leds_state_mind2v1_probe(state, &paths);
}

static bool
led_control_mind2v1_dynamic_probe(leds_state_mind2v1_t *state)
{
    // XXX: No configuration tweaks for now
    (void)state;
    return false;
}

bool
led_control_mind2v1_probe(led_control_t *self)
{
    static leds_state_mind2v1_t state = { };

    bool res = false;

    leds_state_mind2v1_init(&state);

    self->name   = "mind2v1";
    self->data   = &state;
    self->enable = NULL;
    self->blink  = NULL;
    self->value  = led_control_mind2v1_value_cb;
    self->close  = led_control_mind2v1_close_cb;

    // XXX: Disable breathing for now
    self->can_breathe = false;
    self->breath_type = LED_RAMP_DISABLED;

    if( self->use_config )
        res = led_control_mind2v1_dynamic_probe(&state);

    if( !res )
        res = led_control_mind2v1_static_probe(&state);

    if( !res )
        leds_state_mind2v1_quit(&state);

    return res;
}
