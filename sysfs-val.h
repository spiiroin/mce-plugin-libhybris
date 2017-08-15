/** @file sysfs-val.h
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

#ifndef  SYSFS_VAL_H_
# define SYSFS_VAL_H_

#include <stdbool.h>

/* ========================================================================= *
 * TYPES
 * ========================================================================= */

typedef struct sysfsval_t sysfsval_t;

/* ========================================================================= *
 * PROTOS
 * ========================================================================= */

sysfsval_t        *sysfsval_create    (void);
void               sysfsval_delete    (sysfsval_t *self);
bool               sysfsval_open      (sysfsval_t *self, const char *path);
void               sysfsval_close     (sysfsval_t *self);
const char        *sysfsval_path      (const sysfsval_t *self);
int                sysfsval_get       (const sysfsval_t *self);
bool               sysfsval_set       (sysfsval_t *self, int value);
void               sysfsval_assume    (sysfsval_t *self, int value);
void               sysfsval_invalidate(sysfsval_t *self);
bool               sysfsval_refresh   (sysfsval_t *self);

#endif /* SYSFS_VAL_H_ */
