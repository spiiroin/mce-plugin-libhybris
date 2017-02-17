/** @file plugin-logging.h
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

#ifndef  PLUGIN_LOGGING_H_
# define PLUGIN_LOGGING_H_

# include <syslog.h>

/** MCE logging priorities
 */
enum
{
  LL_CRIT    = LOG_CRIT,          /**< Critical error */
  LL_ERR     = LOG_ERR,           /**< Error */
  LL_WARN    = LOG_WARNING,       /**< Warning */
  LL_NOTICE  = LOG_NOTICE,        /**< Normal but noteworthy */
  LL_INFO    = LOG_INFO,          /**< Informational message */
  LL_DEBUG   = LOG_DEBUG,         /**< Useful when debugging */
};

void mce_hybris_log(int lev, const char *file, const char *func,
                    const char *fmt, ...) __attribute__ ((format (printf, 4, 5)));

/** Logging from hybris plugin mimics mce-log.h API */
# define mce_log(LEV,FMT,ARGS...) \
   mce_hybris_log(LEV, __FILE__, __FUNCTION__ ,FMT, ## ARGS)

#endif /* PLUGIN_LOGGING_H_ */
