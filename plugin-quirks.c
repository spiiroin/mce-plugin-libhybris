/** @file plugin-quirks.c
 *
 * mce-plugin-libhybris - Libhybris plugin for Mode Control Entity
 * <p>
 * Copyright (C) 2017 Jolla Ltd.
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

#include "plugin-quirks.h"

#include "plugin-config.h"
#include "plugin-logging.h"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

/* ========================================================================= *
 * QUIRKS
 * ========================================================================= */

/** Strings that should be treated as "1" when evaluating quirk settings */
static const char * const quirk_true_lut[] =
{
    "true", "yes", "enabled", 0
};

/** Strings that should be treated as "0" when evaluating quirk settings */
static const char * const quirk_false_lut[] =
{
    "false", "no", "disabled", 0
};

/** Quirk enum id to settings ini-file key lookup table */
static const char * const quirk_name_lut[QUIRK_COUNT] =
{
};

/** Flag array for: quirk setting has been defined in mce config */
static bool quirk_defined_lut[QUIRK_COUNT];

/** Value array for: quirk settings defined in mce config */
static int  quirk_value_lut[QUIRK_COUNT];

/** Helper for checking string exists in an array
 *
 * @param vec  Array of strings
 * @param str  String to look for
 *
 * @return true if string exists in array, false otherwise
 */
static bool
quirk_in_array(const char * const *vec, const char *str)
{
    for( ; *vec; ++vec )
        if( !strcmp(*vec, str) )
            return true;

    return false;
}

/** Helper for parsing integer quirk setting values
 *
 * To allow improved readability, various aliases for boolean
 * type values are converted to true/1 or false/0.
 *
 * @param str  String value from ini-file
 *
 * @return Integer value, or zero as fallback value
 */
static int
quirk_parse_value(const char *str)
{
    int val = false;

    if( !str )
        goto EXIT;

    if( quirk_in_array(quirk_false_lut, str) )
        goto EXIT;

    val = true;

    if( quirk_in_array(quirk_true_lut, str) )
        goto EXIT;

    val = strtol(str, 0, 0);

EXIT:
    return val;
}

/** Populate quirk value array using data from mce configuration files
 */
static void
plugin_quirk_init(void)
{
    for( quirk_t id = 0; id < QUIRK_COUNT; ++id ) {
        gchar *val = plugin_config_get_string(MCE_CONF_LED_CONFIG_HYBRIS_GROUP,
                                              quirk_name_lut[id], 0);
        if( !val )
            continue;

        quirk_defined_lut[id] = true;
        quirk_value_lut[id] = quirk_parse_value(val);
        mce_log(LOG_DEBUG, "set %s = %d",
                quirk_name_lut[id],
                quirk_value_lut[id]);
        g_free(val);
    }
}

/** Predicat for: numerical quirk id is valid
 *
 * @param id  Quirk ID
 *
 * @return true if id is valid, false otherwise
 */
static bool
quirk_is_valid(quirk_t id)
{
    return id > QUIRK_INVALID && id < QUIRK_COUNT && quirk_name_lut[id];
}

/** Predicat for: Value for quirk id has been defined in configuration
 *
 * @param id  Quirk ID
 *
 * @return true if id is valid and defined, false otherwise
 */
static bool
quirk_is_defined(quirk_t id)
{
    return quirk_is_valid(id) && quirk_defined_lut[id];
}

/** Get human readable name for a quirk id
 *
 * @param id  Quirk ID
 *
 * @return quirk name used in setting ini-files, or "QuirkInvalid"
 */
const char *
quirk_name(quirk_t id)
{
    const char *repr = 0;

    if( quirk_is_valid(id) )
        repr = quirk_name_lut[id];

    return repr ?: "QuirkInvalid";
}

/* Get value associated with quirk id
 *
 * @param id  Quirk ID
 * @param def Fallback value
 *
 * @return Configured quirk value, or the caller provided default value
 */
int
quirk_value(quirk_t id, int def)
{
    static bool done = false;

    if( !done ) {
        done = true;
        plugin_quirk_init();
    }

    return quirk_is_defined(id) ? quirk_value_lut[id] : def;
}
