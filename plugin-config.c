/** @file plugin-config.c
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

#include "plugin-config.h"

#include "plugin-logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= *
 * SETTINGS
 * ========================================================================= */

gchar *
plugin_config_get_string(const gchar *group,
                         const gchar *key,
                         const gchar *defaultval)
{
    extern gboolean mce_conf_has_key(const gchar *group,
                                     const gchar *key);

    extern gchar *mce_conf_get_string(const gchar *group,
                                      const gchar *key,
                                      const gchar *defaultval);

    gchar *res = 0;

    /* From MCE point of view it is suspicious if code tries to
     * access settings that are not defined and warning is emitted
     * in such cases. Whereas from this plugin point of view all
     * settings are optional -> check that key actually exists before
     * attempting to fetch the value to avoid unwanted logging. */
    if( !mce_conf_has_key(group, key) )
        res = defaultval ? g_strdup(defaultval) : 0;
    else
        res = mce_conf_get_string(group, key, defaultval);

    mce_log(LOG_DEBUG, "[%s] %s = %s", group, key, res ?: "(null)");

    return res;
}

static inline void *lea(const void *base, int offs)
{
    return ((char *)base)+offs;
}

/** Set all configurable dynamic data to null
 *
 * @param cfg configuration lookup table
 * @param obj object to configure
 */
void
objconf_init(const objconf_t *cfg, void *obj)
{
    for( size_t i = 0; cfg[i].type != CONFTYPE_NONE; ++i ) {
        char **memb = lea(obj, cfg[i].off);
        *memb = 0;
    }
}

/** Release all configurable dynamic data
 *
 * @param cfg configuration lookup table
 * @param obj object to configure
 */
void
objconf_quit(const objconf_t *cfg, void *obj)
{
    for( size_t i = 0; cfg[i].type != CONFTYPE_NONE; ++i ) {
        char **memb = lea(obj, cfg[i].off);
        free(*memb), *memb = 0;
    }
}

/** Parse all configurable dynamic data
 *
 * @param cfg configuration lookup table
 * @param obj object to configure
 *
 * @return true if at least one member was set, false otherwise
 */
bool
objconf_parse(const objconf_t *cfg, void *obj, const char *chn)
{
    int    set = 0;
    gchar *dir = 0;

    char tmp[256];

    /* Fetch channel/led directory in form of:
     *
     * <CHANNEL><Directory>=/sys/class/leds/red
     *
     * Where CHANNEL depends on backend but should be something
     * like "Led" for single channel leds and "Red", "Green" etc
     * for multichannel leds.
     */
    snprintf(tmp, sizeof tmp, "%sDirectory", chn);
    dir = plugin_config_get_string(MCE_CONF_LED_CONFIG_HYBRIS_GROUP, tmp, 0);

    for( size_t i = 0; cfg[i].type != CONFTYPE_NONE; ++i ) {
        const char *ini_key    = cfg[i].key;
        gchar      *ini_value  = 0;
        char      **cfg_member = lea(obj, cfg[i].off);
        char       *cfg_value  = 0;

        switch( cfg[i].type ) {
        case CONFTYPE_FILE:
            /* Fetch absolute control file path in form of
             *
             * <CHANNEL><MEMBER>File=sys/class/leds/red/brightness
             *
             * Where MEMBER is "Brightness", "MaxBrightness", etc
             */
            snprintf(tmp, sizeof tmp, "%s%sFile", chn, ini_key);
            ini_value = plugin_config_get_string(MCE_CONF_LED_CONFIG_HYBRIS_GROUP,
                                           tmp, 0);
            if( !ini_value && dir ) {
                /* Fetch control file path relative to directory
                 *
                 * <MEMBER>File=brightness
                 */
                snprintf(tmp, sizeof tmp, "%sFile", ini_key);
                ini_value = plugin_config_get_string(MCE_CONF_LED_CONFIG_HYBRIS_GROUP,
                                               tmp, 0);
            }

            if( !ini_value && cfg[i].def ) {
                /* Use defined fallback value */
                ini_value = g_strdup(cfg[i].def);
            }

            if( ini_value ) {
                snprintf(tmp, sizeof tmp, "%s/%s", dir, ini_value);
                cfg_value = strdup(tmp);
            }
            break;

        case CONFTYPE_STRING:
            /* Fetch string value in form of
             * 1. <CHANNEL><MEMBER>=<string>
             * 2. <MEMBER>=<string>
             * 3. defined fallback value
             *
             * Where CHANNEL is "Red", "Green", "Blue", etc
             * Where MEMBER is "OnValue", "OffValue", etc
             */
            snprintf(tmp, sizeof tmp, "%s%s", chn, ini_key);
            ini_value = plugin_config_get_string(MCE_CONF_LED_CONFIG_HYBRIS_GROUP,
                                           tmp, 0);

            if( !ini_value ) {
                snprintf(tmp, sizeof tmp, "%s", ini_key);
                ini_value = plugin_config_get_string(MCE_CONF_LED_CONFIG_HYBRIS_GROUP,
                                               tmp, cfg[i].def);
            }

            if( ini_value )
                cfg_value = strdup(ini_value);
            break;

        default:
            break;
        }
        g_free(ini_value);

        if( cfg_value ) {
            /* Note: The cfg_member is likely to point to a structure member
             *       that has been defined to have 'const char *' type, and
             *       we ignore it here on purpose.
             *
             *       ... because the same structures are used also for legacy
             *       lookup table based approach - which basically requires
             *       using const strings for things to make sense.
             */
            ++set, free(*cfg_member), *cfg_member = cfg_value;
            mce_log(LOG_DEBUG, "%s:%s = %s", chn, ini_key, *cfg_member ?: "NA");
        }
    }

    g_free(dir);

    return set > 0;
}
