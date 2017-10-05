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
    for( size_t i = 0; cfg[i].key; ++i ) {
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
    for( size_t i = 0; cfg[i].key; ++i ) {
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

    for( size_t i = 0; cfg[i].key; ++i ) {
        char **memb = lea(obj, cfg[i].off);

        /* Fetch absolute control file path in form of
         * <CHANNEL><MEMBER>File=sys/class/leds/red/brightness
         *
         * Where MEMBER is "Brightness", "MaxBrightness", etc
         */
        snprintf(tmp, sizeof tmp, "%s%sFile", chn, cfg[i].key);
        gchar *val = plugin_config_get_string(MCE_CONF_LED_CONFIG_HYBRIS_GROUP,
                                              tmp, 0);
        if( val ) {
            snprintf(tmp, sizeof tmp, "%s/%s", dir, val);
            free(*memb), *memb = strdup(tmp);
            g_free(val);
        }
        else if( dir ) {
            /* Fetch control file path relative to directory
             *
             * <MEMBER>File=brightness
             */
            snprintf(tmp, sizeof tmp, "%sFile", cfg[i].key);
            val = plugin_config_get_string(MCE_CONF_LED_CONFIG_HYBRIS_GROUP,
                                           tmp, cfg[i].def);
            if( val ) {
                snprintf(tmp, sizeof tmp, "%s/%s", dir, val);
                free(*memb), *memb = strdup(tmp);
                g_free(val);
            }
        }
        set += (*memb != 0);

        mce_log(LOG_DEBUG, "%s:%s = %s", chn, cfg[i].key, *memb ?: "NA");
    }

    g_free(dir);

    return set > 0;
}
