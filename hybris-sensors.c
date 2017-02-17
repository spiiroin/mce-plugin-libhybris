/** @file hybris-sensors.c
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

#include "hybris-sensors.h"
#include "plugin-logging.h"
#include "hybris-thread.h"

#include <hardware/sensors.h>

#include <glib.h>

/* ========================================================================= *
 * PROTOTYPES
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * SENSORS_PLUGIN
 * ------------------------------------------------------------------------- */

static const struct sensor_t *hybris_plugin_sensors_get_sensor   (int type);

bool                          hybris_plugin_sensors_load         (void);
void                          hybris_plugin_sensors_unload       (void);

static bool                   hybris_plugin_sensors_open_device  (struct sensors_poll_device_t **pdevice);
static void                   hybris_plugin_sensors_close_device (struct sensors_poll_device_t **pdevice);

/* ------------------------------------------------------------------------- *
 * SENSORS_DEVICE
 * ------------------------------------------------------------------------- */

static void                   hybris_device_sensors_thread_cb    (void *aptr);

static bool                   hybris_device_sensors_init         (void);
static void                   hybris_device_sensors_quit         (void);

/* ------------------------------------------------------------------------- *
 * PROXIMITY_SENSOR
 * ------------------------------------------------------------------------- */

bool                          hybris_sensor_ps_init              (void);
void                          hybris_sensor_ps_quit              (void);
void                          hybris_sensor_ps_set_hook          (mce_hybris_ps_fn cb);
bool                          hybris_sensor_ps_set_active        (bool state);

/* ------------------------------------------------------------------------- *
 * AMBIENT_LIGHT_SENSOR
 * ------------------------------------------------------------------------- */

bool                          hybris_device_als_init             (void);
void                          hybris_device_als_quit             (void);
void                          hybris_device_als_set_hook         (mce_hybris_als_fn cb);
bool                          hybris_device_als_set_active       (bool state);

/* ========================================================================= *
 * SENSORS_PLUGIN
 * ========================================================================= */

/** Handle for libhybris sensors plugin */
static struct sensors_module_t *hybris_plugin_sensors_handle = 0;

/** Array of sensors available via hybris_plugin_sensors_handle */
static const struct sensor_t   *hybris_plugin_sensors_lut = 0;

/** Number of sensors available via hybris_plugin_sensors_handle */
static int                      hybris_plugin_sensors_cnt = 0;

/** Pointer to libhybris proximity sensor object */
static const struct sensor_t   *hybris_plugin_sensors_ps_sensor = 0;

/** Pointer to libhybris ambient light sensor object */
static const struct sensor_t   *hybris_plugin_sensors_als_sensor = 0;

/** Helper for locating sensor objects by type
 *
 * @param type SENSOR_TYPE_LIGHT etc
 *
 * @return sensor pointer, or NULL if not available
 */
static const struct sensor_t *
hybris_plugin_sensors_get_sensor(int type)
{
  const struct sensor_t *res = 0;

  for( int i = 0; i < hybris_plugin_sensors_cnt; ++i ) {

    if( hybris_plugin_sensors_lut[i].type == type ) {
      res = &hybris_plugin_sensors_lut[i];
      break;
    }
  }

  return res;
}

/** Load libhybris sensors plugin
 *
 * Also initializes look up table for supported sensors.
 *
 * @return true on success, false on failure
 */
bool
hybris_plugin_sensors_load(void)
{
  static bool done = false;

  if( done ) {
    goto cleanup;
  }

  done = true;

  {
    const struct hw_module_t *mod = 0;
    hw_get_module(SENSORS_HARDWARE_MODULE_ID, &mod);
    hybris_plugin_sensors_handle = (struct sensors_module_t *)mod;
  }

  if( !hybris_plugin_sensors_handle ) {
    mce_log(LL_WARN, "failed top open sensors module");
  }
  else {
    mce_log(LL_DEBUG, "hybris_plugin_sensors_handle = %p",
            hybris_plugin_sensors_handle);
  }

  if( !hybris_plugin_sensors_handle ) {
    goto cleanup;
  }

  hybris_plugin_sensors_cnt = hybris_plugin_sensors_handle->get_sensors_list(hybris_plugin_sensors_handle,
                                                                             &hybris_plugin_sensors_lut);

  hybris_plugin_sensors_als_sensor = hybris_plugin_sensors_get_sensor(SENSOR_TYPE_LIGHT);
  hybris_plugin_sensors_ps_sensor  = hybris_plugin_sensors_get_sensor(SENSOR_TYPE_PROXIMITY);

cleanup:

  return hybris_plugin_sensors_handle != 0;
}

/** Unload libhybris sensors plugin
 */
void
hybris_plugin_sensors_unload(void)
{
  /* cleanup dependencies */
  hybris_device_sensors_quit();

  /* actually unload the module */
  // FIXME: how to unload libhybris modules?
}

/** Convenience function for opening sensors device
 *
 * Similar to what we might or might not have available from hardware/sensors.h
 */
static bool
hybris_plugin_sensors_open_device(struct sensors_poll_device_t **pdevice)
{
  bool ack = false;

  if( !hybris_plugin_sensors_load() ) {
    goto cleanup;
  }

  const struct hw_module_t *mod = &hybris_plugin_sensors_handle->common;
  const char               *id  = SENSORS_HARDWARE_POLL;
  struct hw_device_t       *dev = 0;

  if( !mod->methods->open(mod, id, &dev) ) {
    goto cleanup;
  }

  *pdevice = (struct sensors_poll_device_t *)dev;

  ack = true;

cleanup:

  return ack;
}

/** Convenience function for closing sensors device
 *
 * Similar to what we might or might not have available from hardware/sensors.h
 */
static void
hybris_plugin_sensors_close_device(struct sensors_poll_device_t **pdevice)
{
  struct sensors_poll_device_t *device;

  if( (device = *pdevice) ) {
    *pdevice = 0, device->common.close(&device->common);
  }
}

/* ========================================================================= *
 * SENSORS_DEVICE
 * ========================================================================= */

/** Pointer to libhybris sensor poll device object */
static struct sensors_poll_device_t  *hybris_device_sensors_handle = 0;

/** Callback for forwarding proximity sensor events */
static mce_hybris_ps_fn               hybris_device_sensors_ps_cb  = 0;

/** Callback for forwarding ambient light sensor events */
static mce_hybris_als_fn              hybris_device_sensors_als_cb = 0;

/** Worker thread id */
static pthread_t                      hybris_device_sensors_thread_id = 0;

/** Worker thread for reading sensor events via blocking libhybris interface
 *
 * Note: no mce_log() calls from this function - they are not thread safe
 *
 * @param aptr (thread parameter, not used)
 */
static void
hybris_device_sensors_thread_cb(void *aptr)
{
  (void)aptr;

  sensors_event_t eve[32];

  while( hybris_device_sensors_handle ) {
    /* This blocks until there are events available, or possibly sooner
     * if enabling/disabling sensors changes something. Since we can't
     * guarantee that we ever return from the call, the thread is cancelled
     * asynchronously on cleanup - and any resources possibly reserved by
     * the hybris_device_sensors_handle->poll() are lost. */
    int n = hybris_device_sensors_handle->poll(hybris_device_sensors_handle, eve, G_N_ELEMENTS(eve));

    for( int i = 0; i < n; ++i ) {
      sensors_event_t *e = &eve[i];

      /* Forward data via per sensor callback routines. The callbacks must
       * handle the fact that they get called from the context of the worker
       * thread. */
      switch( e->type ) {
      case SENSOR_TYPE_LIGHT:
        if( hybris_device_sensors_als_cb ) {
          hybris_device_sensors_als_cb(e->timestamp, e->distance);
        }
        break;
      case SENSOR_TYPE_PROXIMITY:
        if( hybris_device_sensors_ps_cb ) {
          hybris_device_sensors_ps_cb(e->timestamp, e->light);
        }
        break;

      case SENSOR_TYPE_ACCELEROMETER:
      case SENSOR_TYPE_MAGNETIC_FIELD:
      case SENSOR_TYPE_ORIENTATION:
      case SENSOR_TYPE_GYROSCOPE:
      case SENSOR_TYPE_PRESSURE:
      case SENSOR_TYPE_TEMPERATURE:
      case SENSOR_TYPE_GRAVITY:
      case SENSOR_TYPE_LINEAR_ACCELERATION:
      case SENSOR_TYPE_ROTATION_VECTOR:
      case SENSOR_TYPE_RELATIVE_HUMIDITY:
      case SENSOR_TYPE_AMBIENT_TEMPERATURE:
        break;
      }
    }
  }
}

/** Initialize libhybris sensor poll device object
 *
 * Also:
 * - disables ALS and PS sensor inputs if possible
 * - starts worker thread to handle sensor input events
 *
 * @return true on success, false on failure
 */
static bool
hybris_device_sensors_init(void)
{
  if( hybris_device_sensors_handle ) {
    goto cleanup;
  }

  if( !hybris_plugin_sensors_open_device(&hybris_device_sensors_handle) ) {
    mce_log(LL_WARN, "failed to open sensor poll device");
    goto cleanup;
  }

  mce_log(LL_DEBUG, "hybris_device_sensors_handle = %p",
          hybris_device_sensors_handle);

  if( hybris_plugin_sensors_ps_sensor ) {
    hybris_device_sensors_handle->activate(hybris_device_sensors_handle, hybris_plugin_sensors_ps_sensor->handle, false);
  }

  if( hybris_plugin_sensors_als_sensor ) {
    hybris_device_sensors_handle->activate(hybris_device_sensors_handle, hybris_plugin_sensors_als_sensor->handle, false);
  }

  hybris_device_sensors_thread_id = hybris_thread_start(hybris_device_sensors_thread_cb, 0);

cleanup:

  return hybris_device_sensors_handle != 0;
}

/** Release libhybris display backlight device object
 *
 * Also:
 * - stops the sensor input worker thread
 * - disables ALS and PS sensor inputs if possible
 */
static void
hybris_device_sensors_quit(void)
{
  if( hybris_device_sensors_handle ) {
    if( hybris_device_sensors_thread_id ) {
      hybris_thread_stop(hybris_device_sensors_thread_id),
      hybris_device_sensors_thread_id = 0;
    }

    if( hybris_plugin_sensors_ps_sensor ) {
      hybris_device_sensors_handle->activate(hybris_device_sensors_handle, hybris_plugin_sensors_ps_sensor->handle, false);
    }

    if( hybris_plugin_sensors_als_sensor ) {
      hybris_device_sensors_handle->activate(hybris_device_sensors_handle, hybris_plugin_sensors_als_sensor->handle, false);
    }

    hybris_plugin_sensors_close_device(&hybris_device_sensors_handle);
  }
}

/* ========================================================================= *
 * PROXIMITY_SENSOR
 * ========================================================================= */

/** Start using proximity sensor via libhybris
 *
 * @return true on success, false on failure
 */
bool
hybris_sensor_ps_init(void)
{
  bool res = false;

  if( !hybris_device_sensors_init() ) {
    goto cleanup;
  }

  if( !hybris_plugin_sensors_ps_sensor ) {
    goto cleanup;
  }

  res = true;

cleanup:

  return res;
}

/** Stop using proximity sensor via libhybris
 *
 * @return true on success, false on failure
 */
void
hybris_sensor_ps_quit(void)
{
  hybris_device_sensors_ps_cb = 0;
}

/** Set callback function for handling proximity sensor events
 *
 * Note: the callback function will be called from worker thread.
 */
void
hybris_sensor_ps_set_hook(mce_hybris_ps_fn cb)
{
  hybris_device_sensors_ps_cb = cb;
}

/** Set proximity sensort input enabled state
 *
 * @param state true to enable input, or false to disable input
 */
bool
hybris_sensor_ps_set_active(bool state)
{
  bool res = false;

  if( !hybris_sensor_ps_init() ) {
    goto cleanup;
  }

  if( hybris_device_sensors_handle->activate(hybris_device_sensors_handle, hybris_plugin_sensors_ps_sensor->handle, state) < 0 ) {
    goto cleanup;
  }

  res = true;

cleanup:

  return res;
}

/* ========================================================================= *
 * AMBIENT_LIGHT_SENSOR
 * ========================================================================= */

/** Start using ambient light sensor via libhybris
 *
 * @return true on success, false on failure
 */
bool
hybris_device_als_init(void)
{
  bool res = false;

  if( !hybris_device_sensors_init() ) {
    goto cleanup;
  }

  if( !hybris_plugin_sensors_als_sensor ) {
    goto cleanup;
  }

  res = true;

cleanup:

  return res;
}

/** Stop using ambient light sensor via libhybris
 *
 * @return true on success, false on failure
 */
void
hybris_device_als_quit(void)
{
  hybris_device_sensors_als_cb = 0;
}

/** Set callback function for handling ambient light sensor events
 *
 * Note: the callback function will be called from worker thread.
 */
void
hybris_device_als_set_hook(mce_hybris_als_fn cb)
{
  hybris_device_sensors_als_cb = cb;
}

/** Set ambient light sensor input enabled state
 *
 * @param state true to enable input, or false to disable input
 */
bool
hybris_device_als_set_active(bool state)
{
  bool res = false;

  if( !hybris_device_als_init() ) {
    goto cleanup;
  }

  if( hybris_device_sensors_handle->activate(hybris_device_sensors_handle, hybris_plugin_sensors_als_sensor->handle, state) < 0 ) {
    goto cleanup;
  }

  res = true;

cleanup:

  return res;
}
