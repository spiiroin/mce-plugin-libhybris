/** @file plugin-config.h
 *
 * mce-plugin-libhybris - Libhybris plugin for Mode Control Entity
 * <p>
 * Copyright (c) 2017 Jolla Ltd.
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

#ifndef  PLUGIN_CONFIG_H_
# define PLUGIN_CONFIG_H_

# include "plugin-logging.h"

# include <stdbool.h>

# include <glib.h>

/* ========================================================================= *
 * CONFIG
 * ========================================================================= */

/** Configuration group for mce-plugin-libhybris related values */
#define MCE_CONF_LED_CONFIG_HYBRIS_GROUP   "LEDConfigHybris"

/** Name of the LED backend to use */
#define MCE_CONF_LED_CONFIG_HYBRIS_BACKEND "BackEnd"

/** Optional enable/disable sw breathing setting */
#define MCE_CONF_LED_CONFIG_HYBRIS_BREATHING_ENABLED "QuirkBreathing"

/** Optional sw breathing type setting */
#define MCE_CONF_LED_CONFIG_HYBRIS_BREATHING_TYPE   "QuirkBreathingType"

gchar * plugin_config_get_string(const gchar *group, const gchar *key, const gchar *defaultval);

typedef enum
{
    /** Item is not valid / is a sentinel */
    CONFTYPE_NONE,

    /** Item is a file path */
    CONFTYPE_FILE,

    /** Item is a standalone string value */
    CONFTYPE_STRING,
} conftype_t;

/** Inifile to object member mapping info */
typedef struct
{
    /** Type of configuration value */
    conftype_t  type;

    /** Ini-file key */
    const char *key;

    /** Fallback value in case key is not defined */
    const char *def;

    /** Offset within object where to store string value */
    off_t       off;
} objconf_t;

/** Object configuration entry sentinel */
#define OBJCONF_STOP \
     {\
         .type = CONFTYPE_NONE,\
     }

/** Object configuration entry for standalone string value
 */
#define OBJCONF_STRING(obj_,memb_,key_,def_)\
     {\
         .type = CONFTYPE_STRING,\
         .key = #key_,\
         .def = def_,\
         .off = offsetof(obj_,memb_),\
     }

/** Object configuration entry for file path
 *
 * For object->member matches /sys/dir/member
 */
#define OBJCONF_FILE(obj_,memb_,key_)\
     {\
         .type = CONFTYPE_FILE,\
         .key = #key_,\
         .def = #memb_,\
         .off = offsetof(obj_,memb_),\
     }

/** Object configuration entry for file path
 *
 * For object->member does not match /sys/dir/member
 */
#define OBJCONF_FILE_EX(obj_,memb_,key_,def_)\
     {\
         .type = CONFTYPE_FILE,\
         .key = #key_,\
         .def = def_,\
         .off = offsetof(obj_,memb_),\
     }

void objconf_init(const objconf_t *cfg, void *obj);
void objconf_quit(const objconf_t *cfg, void *obj);
bool objconf_parse(const objconf_t *cfg, void *obj, const char *chn);

#endif /* PLUGIN_CONFIG_H_ */
