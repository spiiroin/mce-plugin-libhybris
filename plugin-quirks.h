/** @file plugin-quirks.h
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

#ifndef  PLUGIN_QUIRKS_H_
# define PLUGIN_QUIRKS_H_

# include "plugin-logging.h"

/* ========================================================================= *
 * QUIRKS
 * ========================================================================= */

/** Quirk setting IDs */
typedef enum
{
    /** Placeholder ID */
    QUIRK_INVALID = -1,

    /** Override sw breathing desicion made by led backend */
    QUIRK_BREATHING,

    /** Number of quirks */
    QUIRK_COUNT
} quirk_t;

const char *quirk_name(quirk_t id);

int quirk_value(quirk_t id, int def);

/** Helper for caching quirk value locally and logging use for debug purposes
 */
#define QUIRK(id,def) ({\
    static bool done = false;\
    static int  value = 0;\
    if( !done ) {\
        done = true;\
        value = quirk_value((id),(def));\
        mce_log(LOG_DEBUG, "use %s = %d",\
                quirk_name((id)), value);\
    }\
    value;\
})

#endif /* PLUGIN_QUIRKS_H_ */
