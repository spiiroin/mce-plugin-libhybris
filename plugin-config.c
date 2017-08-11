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
        goto EXIT;

    if( (res = mce_conf_get_string(group, key, defaultval)) )
        mce_log(LOG_DEBUG, "[%s] %s = %s", group, key, res ?: "(null)");

EXIT:
    return res;
}
