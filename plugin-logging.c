/** @file plugin-logging.c
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

#include "plugin-logging.h"

#include "plugin-api.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/* ========================================================================= *
 * PROTOTYPES
 * ========================================================================= */

void mce_hybris_set_log_hook(mce_hybris_log_fn cb);
void mce_hybris_log         (int lev, const char *file, const char *func, const char *fmt, ...);

/* ========================================================================= *
 * DATA
 * ========================================================================= */

/** Callback function for diagnostic output, or NULL for stderr output */
static mce_hybris_log_fn mce_hybris_log_cb = 0;

/* ========================================================================= *
 * FUNCTIONS
 * ========================================================================= */

/** Set diagnostic output forwarding callback
 *
 * @param cb  The callback function to use, or NULL for stderr output
 */
void
mce_hybris_set_log_hook(mce_hybris_log_fn cb)
{
  mce_hybris_log_cb = cb;
}

/** Wrapper for diagnostic logging
 *
 * @param lev  syslog priority (=mce_log level) i.e. LL_ERR etc
 * @param file source code path
 * @param func name of function within file
 * @param fmt  printf compatible format string
 * @param ...  parameters required by the format string
 */
void
mce_hybris_log(int lev, const char *file, const char *func,
               const char *fmt, ...)
{
  char *msg = 0;
  va_list va;

  va_start(va, fmt);
  if( vasprintf(&msg, fmt, va) < 0 ) msg = 0;
  va_end(va);

  if( msg ) {
    if( mce_hybris_log_cb ) {
      mce_hybris_log_cb(lev, file, func, msg);
    }
    else {
      fprintf(stderr, "%s: %s: %s\n", file, func, msg);
    }
    free(msg);
  }
}
