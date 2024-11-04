/** @file sysfs-led-util.c
 *
 * mce-plugin-libhybris - Libhybris plugin for Mode Control Entity
 * <p>
 * Copyright (c) 2013 - 2017 Jolla Ltd.
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

#include "sysfs-led-util.h"

#include "plugin-logging.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* ========================================================================= *
 * PROTOTYPES
 * ========================================================================= */

int  led_util_read_number(const char *path);
void led_util_close_file (int *fd_ptr);
bool led_util_open_file  (int *fd_ptr, const char *path);
int  led_util_scale_value(int in, int max);
int  led_util_gcd        (int a, int b);
int  led_util_roundup    (int val, int range);

/* ========================================================================= *
 * FUNCTIONS
 * ========================================================================= */

/** Read number from file
 */
int
led_util_read_number(const char *path)
{
  int res = -1;
  int fd  = -1;
  char tmp[64];

  if( (fd = open(path, O_RDONLY)) == -1 ) {
    goto cleanup;
  }
  int rc = read(fd, tmp, sizeof tmp - 1);
  if( rc < 0 ) {
    goto cleanup;
  }
  tmp[rc] = 0;
  res = strtol(tmp, 0, 0);

cleanup:

  if( fd != -1 ) close(fd);

  return res;
}

/** Close led sysfs control file
 */
void
led_util_close_file(int *fd_ptr)
{
  if( fd_ptr && *fd_ptr != -1 )
  {
    close(*fd_ptr), *fd_ptr = -1;
  }
}

/** Open led sysfs control file in append mode
 */
bool
led_util_open_file(int *fd_ptr, const char *path)
{
  bool res = false;

  led_util_close_file(fd_ptr);

  if( fd_ptr && path )
  {
    if( (*fd_ptr = open(path, O_WRONLY|O_APPEND)) != -1 )
    {
      res = true;
    }
    else if( errno != ENOENT )
    {
      mce_log(LL_WARN, "%s: %s: %m", path, "open");
    }
  }

  return res;
}

/** Scale value from 0...255 to 0...max range
 *
 * Note: zero / nonzero nature of input is preserved in output
 */
int
led_util_scale_value(int in, int max)
{
  int out = 0;
  if( in > 0 )
    out = led_util_trans(in, 1, 255, 1, max);
  return out;
}

/** Calculate the greatest common divisor of two integer numbers
 */
int
led_util_gcd(int a, int b)
{
  int t;
  /* Use abs values; will fail if a or b < -INT_MAX */
  if( a < 0 ) a = -a;
  if( b < 0 ) b = -b;

  /* Make it so that: a >= b */
  if( a < b ) t = a, a = b, b = t;

  /* Iterate until a mod b reaches zero */
  while( b )  t = a % b, a = b, b = t;

  /* Do not return zero even on a == b == 0 */
  return a ?: 1;
}

/** Round up number to the next multiple of given step size
 */
int
led_util_roundup(int val, int range)
{
  int extra = val % range;
  if( extra ) extra = range - extra;
  return val + extra;
}
