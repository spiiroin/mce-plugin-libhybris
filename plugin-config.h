/** @file plugin-config.h
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

#ifndef  PLUGIN_CONFIG_H_
# define PLUGIN_CONFIG_H_

# include "plugin-logging.h"

# include <glib.h>

/* ========================================================================= *
 * CONFIG
 * ========================================================================= */

/** Configuration group for mce-plugin-libhybris related values */
#define MCE_CONF_LED_CONFIG_HYBRIS_GROUP   "LEDConfigHybris"

/** Name of the LED backend to use */
#define MCE_CONF_LED_CONFIG_HYBRIS_BACKEND "BackEnd"

gchar * plugin_config_get_string(const gchar *group, const gchar *key, const gchar *defaultval);

#endif /* PLUGIN_CONFIG_H_ */
