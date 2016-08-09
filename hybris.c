/* ------------------------------------------------------------------------- *
 * Copyright (C) 2013 Jolla Ltd.
 * Contact: Simo Piiroinen <simo.piiroinen@jollamobile.com>
 * License: LGPLv2.1
 * ------------------------------------------------------------------------- */

/* ========================================================================= *
 * This module implements "mce-libhybris-plugin" for use from
 * "mce-libhybris-module" within mce.
 *
 * The idea of "hybris-plugin" is shortly:
 * - it uses no mce functions or data types
 * - it can be compiled independently from mce
 * - it exposes no libhybris/android datatypes / functions
 *
 * And the idea of "hybris-module" is:
 * - it contains functions with the same names as "hybris-plugin"
 * - if called, the functions will load & call "hybris-plugin" code
 * - if "hybris-plugin" is not present "hybris-module" functions
 *   still work, but return failures for everything
 *
 * Put together:
 * - mce code can assume that libhybris code is always available and
 *   callable during hw probing activity
 * - if hybris plugin is not installed (or if some hw is not supported
 *   by the underlying android code), failures will be reported and mce
 *   can try other existing ways to proble hw controls
 * ========================================================================= */

#define MCE_HYBRIS_INTERNAL 2
#include "mce-hybris.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <syslog.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>

#include <glib.h>

#include <system/window.h>
#include <hardware/lights.h>
#include <hardware/fb.h>
#include <hardware/sensors.h>

static void mce_hybris_sensors_quit(void);

static void mce_hybris_log(int lev, const char *file,
                           const char *func, const char *fmt,
                           ...) __attribute__ ((format (printf, 4, 5)));

/* ========================================================================= *
 * LOGGING
 * ========================================================================= */

/** Callback function for diagnostic output, or NULL for stderr output */
static mce_hybris_log_fn log_cb = 0;

/** Set diagnostic output forwarding callback
 *
 * @param cb  The callback function to use, or NULL for stderr output
 */
void mce_hybris_set_log_hook(mce_hybris_log_fn cb)
{
  log_cb = cb;
}

/** Wrapper for diagnostic logging
 *
 * @param lev  syslog priority (=mce_log level) i.e. LOG_ERR etc
 * @param file source code path
 * @param func name of function within file
 * @param fmt  printf compatible format string
 * @param ...  parameters required by the format string
 */
static void mce_hybris_log(int lev, const char *file, const char *func,
                           const char *fmt, ...)
{
  char *msg = 0;
  va_list va;

  va_start(va, fmt);
  if( vasprintf(&msg, fmt, va) < 0 ) msg = 0;
  va_end(va);

  if( msg ) {
    if( log_cb ) log_cb(lev, file, func, msg);
    else         fprintf(stderr, "%s: %s: %s\n", file, func, msg);
    free(msg);
  }
}

/** Logging from hybris plugin mimics mce-log.h API */
#define mce_log(LEV,FMT,ARGS...) \
   mce_hybris_log(LEV, __FILE__, __FUNCTION__ ,FMT, ## ARGS)

/* ========================================================================= *
 * THREAD helpers
 * ========================================================================= */

/** Thread start details; used for inserting custom thread setup code */
typedef struct
{
  void  *data;
  void (*func)(void *);
} gate_t;

/** Mutex used for synchronous worker thread startup */
static pthread_mutex_t gate_mutex = PTHREAD_MUTEX_INITIALIZER;

/** Condition used for signaling worker thread startup */
static pthread_cond_t  gate_cond  = PTHREAD_COND_INITIALIZER;

/** Wrapper for starting new worker thread
 *
 * For use from mce_hybris_start_thread().
 *
 * Before the actual thread start routine is called, the
 * new thread is put in to asynchronously cancellabe state
 * and the starter is woken up via condition.
 *
 * @param aptr wrapper data as void pointer
 *
 * @return 0 on thread exit - via pthread_join()
 */
static void *gate_start(void *aptr)
{
  gate_t *gate = aptr;

  void  (*func)(void*);
  void   *data;

  /* Allow quick and dirty cancellation */
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0);

  /* Tell thread gate we're up and running */
  pthread_mutex_lock(&gate_mutex);
  pthread_cond_broadcast(&gate_cond);
  pthread_mutex_unlock(&gate_mutex);

  /* Collect data we need, release rest */
  func = gate->func;
  data = gate->data;
  free(gate), gate = 0;

  /* Call the real thread start */
  func(data);

  return 0;
}

/** Helper for starting new worker thread
 *
 * @param start function to call from new thread
 * @param arg   data to pass to start function
 *
 * @return thread id on success, or 0 on error
 */
static pthread_t mce_hybris_start_thread(void (*start)(void *),
                                         void* arg)
{
  pthread_t  res = 0;
  gate_t   *gate = 0;

  if( !(gate = calloc(1, sizeof gate)) ) {
    goto EXIT;
  }

  gate->data = arg;
  gate->func = start;

  pthread_mutex_lock(&gate_mutex);

  if( pthread_create(&res, 0, gate_start, gate) != 0 ) {
    mce_log(LOG_ERR, "could not start worker thread");

    /* content of res is undefined on failure, force to zero */
    res = 0;
  }
  else {
    /* wait until thread has had time to start and set
     * up the cancellation parameters */
    mce_log(LOG_DEBUG, "waiting worker to start ...");
    pthread_cond_wait(&gate_cond, &gate_mutex);
    mce_log(LOG_DEBUG, "worker started");

    /* the thread owns the gate now */
    gate = 0;
  }

  pthread_mutex_unlock(&gate_mutex);

EXIT:

  free(gate);

  return res;
}

/* ========================================================================= *
 * FRAMEBUFFER module
 * ========================================================================= */

/** Handle for libhybris framebuffer plugin */
static const struct hw_module_t    *mod_fb = 0;

/** Pointer to libhybris frame buffer device object */
static struct framebuffer_device_t *dev_fb = 0;

/** Load libhybris framebuffer plugin
 *
 * @return true on success, false on failure
 */
static bool mce_hybris_modfb_load(void)
{
  static bool done = false;

  if( !done ) {
    done = true;
    hw_get_module(GRALLOC_HARDWARE_FB0, &mod_fb);
    if( !mod_fb ) {
      mce_log(LOG_WARNING, "failed to open frame buffer module");
    }
    else {
      mce_log(LOG_DEBUG, "mod_fb = %p", mod_fb);
    }
  }

  return mod_fb != 0;
}

/** Unload libhybris framebuffer plugin
 */
static void mce_hybris_modfb_unload(void)
{

  /* cleanup dependencies */
  mce_hybris_framebuffer_quit();

  /* actually unload the module */
  // FIXME: how to unload libhybris modules?
}

/* ------------------------------------------------------------------------- *
 * framebuffer device
 * ------------------------------------------------------------------------- */

/** Convenience function for opening frame buffer device
 *
 * Similar to what we might or might not have available from hardware/fb.h
 */
static int
mce_framebuffer_open(const struct hw_module_t* module,
                     struct framebuffer_device_t** device)
{
  return module->methods->open(module, GRALLOC_HARDWARE_FB0,
                               (struct hw_device_t**)device);
}

/** Convenience function for closing frame buffer device
 *
 * Similar to what we might or might not have available from hardware/fb.h
 */
static int
mce_framebuffer_close(struct framebuffer_device_t* device)
{
  return device->common.close(&device->common);
}

/** Initialize libhybris frame buffer device object
 *
 * @return true on success, false on failure
 */
bool mce_hybris_framebuffer_init(void)
{
  static bool done = false;

  if( !done ) {
    done = true;

    if( !mce_hybris_modfb_load() ) {
      goto cleanup;
    }

    mce_framebuffer_open(mod_fb, &dev_fb);
    if( !dev_fb ) {
      mce_log(LOG_ERR, "failed to open framebuffer device");
    }
    else {
      mce_log(LOG_DEBUG, "dev_fb = %p", dev_fb);
    }
  }

cleanup:
  return dev_fb != 0;
}

/** Release libhybris frame buffer device object
 */
void mce_hybris_framebuffer_quit(void)
{
  if( dev_fb ) {
    mce_framebuffer_close(dev_fb), dev_fb = 0;
  }
}

/** Set frame buffer power state via libhybris
 *
 * @param state true to power on, false to power off
 *
 * @return true on success, false on failure
 */
bool mce_hybris_framebuffer_set_power(bool state)
{
  bool ack = false;

  if( !mce_hybris_framebuffer_init() ) {
    goto cleanup;
  }

  if( dev_fb->enableScreen(dev_fb, state) < 0 ) {
    goto cleanup;
  }

  ack = true;

cleanup:
  return ack;
}

/* ========================================================================= *
 * LIGHTS module
 * ========================================================================= */

/** Handle for libhybris lights plugin */
static const struct hw_module_t *mod_lights    = 0;

/** Pointer to libhybris frame display backlight device object */
static struct light_device_t    *dev_backlight = 0;

/** Pointer to libhybris frame keypad backlight device object */
static struct light_device_t    *dev_keypad    = 0;

/** Pointer to libhybris frame indicator led device object */
static struct light_device_t    *dev_indicator = 0;

/** Load libhybris lights plugin
 *
 * @return true on success, false on failure
 */
static bool mce_hybris_modlights_load(void)
{
  static bool done = false;

  if( !done ) {
    done = true;
    hw_get_module(LIGHTS_HARDWARE_MODULE_ID, &mod_lights);
    if( !mod_lights ) {
      mce_log(LOG_WARNING, "failed to open lights module");
    }
    else {
      mce_log(LOG_DEBUG, "mod_lights = %p", mod_lights);
    }
  }

  return mod_lights != 0;
}

/** Unload libhybris lights plugin
 */
static void mce_hybris_modlights_unload(void)
{
  /* cleanup dependencies */
  mce_hybris_backlight_quit();
  mce_hybris_keypad_quit();
  mce_hybris_indicator_quit();

  /* actually unload the module */
  // FIXME: how to unload libhybris modules?
}

/* ========================================================================= *
 * LIGHTS module: display backlight device
 * ========================================================================= */

/** Convenience function for opening a light device
 *
 * Similar to what we might or might not have available from hardware/lights.h
 */
static int
mce_light_device_open(const struct hw_module_t* module, const char *id,
                      struct light_device_t** device)
{
  return module->methods->open(module, id, (struct hw_device_t**)device);
}

/** Convenience function for closing a light device
 *
 * Similar to what we might or might not have available from hardware/lights.h
 */
static void
mce_light_device_close(const struct light_device_t *device)
{
    device->common.close((struct hw_device_t*) device);
}

/** Initialize libhybris display backlight device object
 *
 * @return true on success, false on failure
 */
bool mce_hybris_backlight_init(void)
{
  static bool done = false;

  if( !done ) {
    done = true;

    if( !mce_hybris_modlights_load() ) {
      goto cleanup;
    }

    mce_light_device_open(mod_lights, LIGHT_ID_BACKLIGHT, &dev_backlight);

    if( !dev_backlight ) {
      mce_log(LOG_WARNING, "failed to open backlight device");
    }
    else {
      mce_log(LOG_DEBUG, "%s() -> %p", __FUNCTION__, dev_backlight);
    }
  }

cleanup:
  return dev_backlight != 0;
}

/** Release libhybris display backlight device object
 */
void mce_hybris_backlight_quit(void)
{
  if( dev_backlight ) {
    mce_light_device_close(dev_backlight), dev_backlight = 0;
  }
}

/** Set display backlight brightness via libhybris
 *
 * @param level 0=off ... 255=maximum brightness
 *
 * @return true on success, false on failure
 */
bool mce_hybris_backlight_set_brightness(int level)
{
  bool     ack = false;
  unsigned lev = (level < 0) ? 0 : (level > 255) ? 255 : level;

  struct light_state_t lst;

  if( !mce_hybris_backlight_init() ) {
    goto cleanup;
  }

  memset(&lst, 0, sizeof lst);
  lst.color          = (0xff << 24) | (lev << 16) | (lev << 8) | (lev << 0);
  lst.flashMode      = LIGHT_FLASH_NONE;
  lst.flashOnMS      = 0;
  lst.flashOffMS     = 0;
  lst.brightnessMode = BRIGHTNESS_MODE_USER;

  if( dev_backlight->set_light(dev_backlight, &lst) < 0 ) {
    goto cleanup;
  }

  ack = true;

cleanup:

  mce_log(LOG_DEBUG, "%s(%d) -> %s", __FUNCTION__, level, ack ? "success" : "failure");

  return ack;
}

/* ========================================================================= *
 * LIGHTS module: keypad backlight device
 * ========================================================================= */

/** Initialize libhybris keypad backlight device object
 *
 * @return true on success, false on failure
 */
bool mce_hybris_keypad_init(void)
{
  static bool done = false;

  if( !done ) {
    done = true;

    if( !mce_hybris_modlights_load() ) {
      goto cleanup;
    }

    mce_light_device_open(mod_lights, LIGHT_ID_KEYBOARD, &dev_keypad);

    if( !dev_keypad ) {
      mce_log(LOG_WARNING, "failed to open keypad backlight device");
    }
    else {
      mce_log(LOG_DEBUG, "%s() -> %p", __FUNCTION__, dev_keypad);
    }
  }

cleanup:
  return dev_keypad != 0;
}

/** Release libhybris keypad backlight device object
 */
void mce_hybris_keypad_quit(void)
{
  if( dev_keypad ) {
    mce_light_device_close(dev_keypad), dev_keypad = 0;
  }
}

/** Set display keypad brightness via libhybris
 *
 * @param level 0=off ... 255=maximum brightness
 *
 * @return true on success, false on failure
 */
bool mce_hybris_keypad_set_brightness(int level)
{
  bool     ack = false;
  unsigned lev = (level < 0) ? 0 : (level > 255) ? 255 : level;

  struct light_state_t lst;

  if( !mce_hybris_keypad_init() ) {
    goto cleanup;
  }

  memset(&lst, 0, sizeof lst);
  lst.color          = (0xff << 24) | (lev << 16) | (lev << 8) | (lev << 0);
  lst.flashMode      = LIGHT_FLASH_NONE;
  lst.flashOnMS      = 0;
  lst.flashOffMS     = 0;
  lst.brightnessMode = BRIGHTNESS_MODE_USER;

  if( dev_keypad->set_light(dev_keypad, &lst) < 0 ) {
    goto cleanup;
  }

  ack = true;

cleanup:

  mce_log(LOG_DEBUG, "%s(%d) -> %s", __FUNCTION__, level, ack ? "success" : "failure");

  return ack;
}

/* ========================================================================= *
 * LIGHTS module: indicator led device
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * generic led utils
 * ------------------------------------------------------------------------- */

/** Read number from file */
static int led_util_read_number(const char *path)
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
static void led_util_close_file(int *fd_ptr)
{
  if( fd_ptr && *fd_ptr != -1 )
  {
    close(*fd_ptr), *fd_ptr = -1;
  }
}

/** Open led sysfs control file in append mode
 */
static bool led_util_open_file(int *fd_ptr, const char *path)
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
      mce_log(LOG_WARNING, "%s: %s: %m", path, "open");
    }
  }

  return res;
}

/** Scale value from 0...255 to 0...max range
 */
static int led_util_scale_value(int in, int max)
{
  int out = (in * max + 128) / 255;

  return (out < 0) ? 0 : (out < max) ? out : max;
}

/* ------------------------------------------------------------------------- *
 * vanilla sysfs controls for one channel in RGB led
 * ------------------------------------------------------------------------- */

typedef struct
{
  const char *max;   // R
  const char *val;   // W
  const char *on;    // W
  const char *off;   // W
  const char *blink; // W
  int         maxval;// value to use if max path is NULL
} led_paths_vanilla_t;

typedef struct
{
  int maxval;
  int fd_val;
  int fd_on;
  int fd_off;
  int fd_blink;

  int cur_val;
  int cur_on;
  int cur_off;
  int cur_blink;

} led_state_vanilla_t;

static void
led_state_vanilla_init(led_state_vanilla_t *self)
{
  self->fd_on     = -1;
  self->fd_off    = -1;
  self->fd_val    = -1;
  self->fd_blink  = -1;
  self->maxval    = -1;

  self->cur_val   = -1;
  self->cur_on    = -1;
  self->cur_off   = -1;
  self->cur_blink = -1;
}

static void
led_state_vanilla_close(led_state_vanilla_t *self)
{
  led_util_close_file(&self->fd_on);
  led_util_close_file(&self->fd_off);
  led_util_close_file(&self->fd_val);
  led_util_close_file(&self->fd_blink);
}

static bool
led_state_vanilla_probe(led_state_vanilla_t *self,
                        const led_paths_vanilla_t *path)
{
  bool res = false;

  led_state_vanilla_close(self);

  // maximum brightness can be read from file or given in config
  if( path->max )
  {
    self->maxval = led_util_read_number(path->max);
  }
  else
  {
    self->maxval = path->maxval;
  }

  if( self->maxval <= 0 )
  {
    goto cleanup;
  }

  // we always must have brightness control
  if( !led_util_open_file(&self->fd_val, path->val) )
  {
    goto cleanup;
  }

  // on/off period controls are optional, but both
  // are needed if one is present
  if( led_util_open_file(&self->fd_on, path->on) )
  {
    if( !led_util_open_file(&self->fd_off, path->off) )
    {
      led_util_close_file(&self->fd_on);
    }
  }

  // having "blink" control file is optional
  led_util_open_file(&self->fd_blink, path->blink);

  res = true;

cleanup:

  if( !res ) led_state_vanilla_close(self);

  return res;
}

static void
led_state_vanilla_set_value(led_state_vanilla_t *self,
                            int value)
{
  if( self->fd_val != -1 )
  {
    value = led_util_scale_value(value, self->maxval);
    if( self->cur_val != value )
    {
      self->cur_val   = value;
      self->cur_blink = -1;
      dprintf(self->fd_val, "%d", value);
    }
  }

  if( self->fd_blink != -1 )
  {
    int blink = (self->cur_on > 0 && self->cur_off > 0);
    if( self->cur_blink != blink )
    {
      self->cur_blink = blink;
      dprintf(self->fd_blink, "%d", blink);
    }
  }
}
static void
led_state_vanilla_set_blink(led_state_vanilla_t *self,
                            int on_ms, int off_ms)
{
  /* Note: Blinking config is taken in use when brightness
   *       sysfs is written to -> we need to invalidate
   *       cached brightness value if blinking changes
   *       are made.
   */

  if( self->fd_on != -1 && self->cur_on != on_ms )
  {
    self->cur_on    = on_ms;
    self->cur_val   = -1;
    self->cur_blink = -1;
    dprintf(self->fd_on,  "%d", on_ms);
  }

  if( self->fd_off != -1 && self->cur_off != off_ms )
  {
    self->cur_off   = off_ms;
    self->cur_val   = -1;
    self->cur_blink = -1;
    dprintf(self->fd_off, "%d", off_ms);
  }
}

/* ------------------------------------------------------------------------- *
 * hammerhead sysfs controls for one channel in RGB led
 * ------------------------------------------------------------------------- */

typedef struct
{
  const char *max;    // R
  const char *val;    // W
  const char *on_off; // W
  const char *enable; // W
} led_paths_hammerhead_t;

typedef struct
{
  int maxval;
  int fd_val;
  int fd_on_off;
  int fd_enable;
} led_state_hammerhead_t;

static void
led_state_hammerhead_init(led_state_hammerhead_t *self)
{
  self->maxval    = -1;
  self->fd_val    = -1;
  self->fd_on_off = -1;
  self->fd_enable = -1;
}

static void
led_state_hammerhead_close(led_state_hammerhead_t *self)
{
  led_util_close_file(&self->fd_val);
  led_util_close_file(&self->fd_on_off);
  led_util_close_file(&self->fd_enable);
}

static bool
led_state_hammerhead_probe(led_state_hammerhead_t *self,
                           const led_paths_hammerhead_t *path)
{
  bool res = false;

  led_state_hammerhead_close(self);

  if( (self->maxval = led_util_read_number(path->max)) <= 0 )
  {
    goto cleanup;
  }

  if( !led_util_open_file(&self->fd_val,    path->val)    ||
      !led_util_open_file(&self->fd_on_off, path->on_off) ||
      !led_util_open_file(&self->fd_enable, path->enable) )
  {
    goto cleanup;
  }

  res = true;

cleanup:

  if( !res ) led_state_hammerhead_close(self);

  return res;
}

static void
led_state_hammerhead_set_enabled(const led_state_hammerhead_t *self,
                                 bool enable)
{
  if( self->fd_enable != -1 )
  {
    dprintf(self->fd_enable, "%d", enable);
  }
}

static void
led_state_hammerhead_set_value(const led_state_hammerhead_t *self,
                               int value)
{
  if( self->fd_val != -1 )
  {
    dprintf(self->fd_val, "%d", led_util_scale_value(value, self->maxval));
  }
}

static void
led_state_hammerhead_set_blink(const led_state_hammerhead_t *self,
                               int on_ms, int off_ms)
{
  if( self->fd_on_off != -1 )
  {
    char tmp[32];
    int len = snprintf(tmp, sizeof tmp, "%d %d", on_ms, off_ms);
    if( len > 0 && len <= (int)sizeof tmp )
    {
      if( write(self->fd_on_off, tmp, len) < 0 ) {
        // dontcare, keep compiler from complaining too
      }
    }
  }
}

/* ------------------------------------------------------------------------- *
 * htcvision sysfs controls for one channel in amber/green led
 * ------------------------------------------------------------------------- */

typedef struct
{
  const char *max;    // R
  const char *val;    // W
  const char *blink;  // W
} led_paths_htcvision_t;

typedef struct
{
  int maxval;
  int fd_val;
  int fd_blink;
} led_state_htcvision_t;

static void
led_state_htcvision_init(led_state_htcvision_t *self)
{
  self->maxval   = -1;
  self->fd_val   = -1;
  self->fd_blink = -1;
}

static void
led_state_htcvision_close(led_state_htcvision_t *self)
{
  led_util_close_file(&self->fd_val);
  led_util_close_file(&self->fd_blink);
}

static bool
led_state_htcvision_probe(led_state_htcvision_t *self,
                          const led_paths_htcvision_t *path)
{
  bool res = false;

  led_state_htcvision_close(self);

  if( (self->maxval = led_util_read_number(path->max)) <= 0 )
  {
    goto cleanup;
  }

  if( !led_util_open_file(&self->fd_val,   path->val)  ||
      !led_util_open_file(&self->fd_blink, path->blink) )
  {
    goto cleanup;
  }

  res = true;

cleanup:

  if( !res ) led_state_htcvision_close(self);

  return res;
}

static void
led_state_htcvision_set_value(const led_state_htcvision_t *self,
                              int value)
{
  if( self->fd_val != -1 )
  {
    dprintf(self->fd_val, "%d", led_util_scale_value(value, self->maxval));
  }
}

static void
led_state_htcvision_set_blink(const led_state_htcvision_t *self, int blink)
{
  if( self->fd_blink != -1 )
  {
    dprintf(self->fd_val, "%d", blink);
  }
}

/* ------------------------------------------------------------------------- *
 * sysfs controls for binary on/off led
 * ------------------------------------------------------------------------- */

typedef struct
{
  const char *val;    // W
} led_paths_binary_t;

typedef struct
{
  int maxval;
  int fd_val;
  int val_last;
} led_state_binary_t;

static void
led_state_binary_init(led_state_binary_t *self)
{
  self->maxval   = -1;
  self->fd_val   = -1;
  self->val_last = -1;
}

static void
led_state_binary_close(led_state_binary_t *self)
{
  led_util_close_file(&self->fd_val);
}

static bool
led_state_binary_probe(led_state_binary_t *self,
                       const led_paths_binary_t *path)
{
  bool res = false;

  led_state_binary_close(self);

  self->maxval = 1;

  if( !led_util_open_file(&self->fd_val,   path->val) )
  {
    goto cleanup;
  }

  res = true;

cleanup:

  if( !res ) led_state_binary_close(self);

  return res;
}

static void
led_state_binary_set_value(led_state_binary_t *self,
                           int value)
{
  if( self->fd_val != -1 )
  {
    int scaled = led_util_scale_value(value, self->maxval);
    if( self->val_last != scaled ) {
      self->val_last = scaled;
      dprintf(self->fd_val, "%d", scaled);
    }
  }
}

/* ------------------------------------------------------------------------- *
 * RGB led control: generic frontend
 * ------------------------------------------------------------------------- */

typedef enum {
  /** Used when sw breathing is not used */
  LED_RAMP_DISABLED  = 0,

  /** The default half sine curve */
  LED_RAMP_HALF_SINE = 1,

  /** Step function used for emulating blinking via sw breathing */
  LED_RAMP_HARD_STEP = 2,
} led_ramp_t;

typedef struct led_control_t led_control_t;

struct led_control_t
{
  const char *name;
  void       *data;
  bool        can_breathe;
  led_ramp_t  breath_type;
  void      (*enable)(void *data, bool enable);
  void      (*blink)(void *data, int on_ms, int off_ms);
  void      (*value)(void *data, int r, int g, int b);
  void      (*close)(void *data);
};

static bool led_control_vanilla_probe(led_control_t *self);
static bool led_control_hammerhead_probe(led_control_t *self);
static bool led_control_htcvision_probe(led_control_t *self);
static bool led_control_binary_probe(led_control_t *self);

/** Set RGB LED enabled/disable
 *
 * @param self   control object
 * @param enable true for enable, or false for disable
 */
static void
led_control_enable(led_control_t *self, bool enable)
{
  if( self->enable )
  {
    self->enable(self->data, enable);
  }
}

/** Set RGB LED blinking period
 *
 * If both on and off are greater than zero, then the PWM generator
 * is used to full intensity blinking. Otherwise it is used for
 * adjusting the LED brightness.
 *
 * @param self control object
 * @param on   milliseconds on
 * @param off  milliseconds off
 */
static void
led_control_blink(led_control_t *self, int on_ms, int off_ms)
{
  if( self->blink )
  {
    led_control_enable(self, false);
    self->blink(self->data, on_ms, off_ms);
  }
}

/** Set RGB LED color
 *
 * @param self control object
 * @param r    red intensity   (0 ... 255)
 * @param g    green intensity (0 ... 255)
 * @param b    blue intensity  (0 ... 255)
 */
static void
led_control_value(led_control_t *self, int r, int g, int b)
{
  if( self->value )
  {
    led_control_enable(self, false);
    self->value(self->data, r, g, b);
    led_control_enable(self, true);
  }
}

/** Reset RGB led control object
 *
 * Initialize control object to closed but valid state.
 *
 * @param self  uninitialized control object
 */
static void
led_control_init(led_control_t *self)
{
  self->name   = 0;
  self->data   = 0;
  self->enable = 0;
  self->blink  = 0;
  self->value  = 0;
  self->close  = 0;

  /* Assume that it is exceptional if sw breathing can't be supported */
  self->can_breathe = true;
  /* And half sine curve should be used for breathing */
  self->breath_type = LED_RAMP_HALF_SINE;

}

/** Set RGB LED enabled/disable
 *
 * @param self   control object
 * @param enable true for enable, or false for disable
 */

static void
led_control_close(led_control_t *self)
{
  if( self->close )
  {
    self->close(self->data);
  }
  led_control_init(self);
}

/** Probe sysfs for RGB LED controls
 *
 * @param self control object
 *
 * @return true if required control files were available, false otherwise
 */
static bool
led_control_probe(led_control_t *self)
{
  typedef bool (*led_control_probe_fn)(led_control_t *);

  /* The probing should be done in order that minimizes
   * chances of false positives.
   */
  static const led_control_probe_fn lut[] =
  {
    /* The hammerhead backend requires presense of
     * unique 'on_off_ms' and 'rgb_start' files. */
    led_control_hammerhead_probe,

    /* The htc vision backend requires presense of
     * unique 'amber' control directory. */
    led_control_htcvision_probe,

    /* The vanilla backend requires only 'brightness'
     * control file, but still needs three directories
     * to be present for red, green and blue channels. */
    led_control_vanilla_probe,

    /* The binary backend needs just one directory
     * that has 'brightness' control file. */
    led_control_binary_probe,
  };

  for( size_t i = 0; i < G_N_ELEMENTS(lut); ++i )
  {
    if( lut[i](self) ) return true;
  }

  return false;
}

/** Query if backend can support sw breathing
 *
 * @return true if breathing can be enabled, false otherwise
 */
static bool
led_control_can_breathe(const led_control_t *self)
{
  return self->can_breathe;
}

/** Query type of sw breathing backend supports
 *
 * @return LED_RAMP_DISABLED, LED_RAMP_HALF_SINE, or ...
 */
static led_ramp_t
led_control_breath_type(const led_control_t *self)
{
  return self->can_breathe ? self->breath_type : LED_RAMP_DISABLED;
}

/* ------------------------------------------------------------------------- *
 * RGB led control: default backend
 *
 * Three channels, all of which:
 * - must have 'brightness' control file
 * - must have 'max_brightness' control file or nonzero fixed maximum
 * - can have either
 *    o blink on/off delay control files
 *    o blink enabled/disable control control file
 *
 * Assumptions built into code:
 *
 * - The write() calls to sysfs controls return immediately from kernel to
 *   userspace, but the kernel side can stay busy with the change for few
 *   milliseconds -> Frequent intensity changes do not block mce process,
 *   so sw breathing is feasible. Minimum delay between led state changes
 *   must be enforced.
 *
 * - Blink controls for the R, G and B channels are independent. To avoid
 *   "rainbow patterns" when more than one channel is used the blink enabling
 *   for all of the channels as simultaneously as possible.
 * ------------------------------------------------------------------------- */

static void
led_control_vanilla_blink_cb(void *data, int on_ms, int off_ms)
{
  led_state_vanilla_t *state = data;
  led_state_vanilla_set_blink(state + 0, on_ms, off_ms);
  led_state_vanilla_set_blink(state + 1, on_ms, off_ms);
  led_state_vanilla_set_blink(state + 2, on_ms, off_ms);
}

static void
led_control_vanilla_value_cb(void *data, int r, int g, int b)
{
  led_state_vanilla_t *state = data;
  led_state_vanilla_set_value(state + 0, r);
  led_state_vanilla_set_value(state + 1, g);
  led_state_vanilla_set_value(state + 2, b);
}

static void
led_control_vanilla_close_cb(void *data)
{
  led_state_vanilla_t *state = data;
  led_state_vanilla_close(state + 0);
  led_state_vanilla_close(state + 1);
  led_state_vanilla_close(state + 2);
}

static bool
led_control_vanilla_probe(led_control_t *self)
{
  /** Sysfs control paths for RGB leds */
  static const led_paths_vanilla_t paths[][3] =
  {
    // vanilla
    {
      {
        .on  = "/sys/class/leds/led:rgb_red/blink_delay_on",
        .off = "/sys/class/leds/led:rgb_red/blink_delay_off",
        .val = "/sys/class/leds/led:rgb_red/brightness",
        .max = "/sys/class/leds/led:rgb_red/max_brightness",
      },
      {
        .on  = "/sys/class/leds/led:rgb_green/blink_delay_on",
        .off = "/sys/class/leds/led:rgb_green/blink_delay_off",
        .val = "/sys/class/leds/led:rgb_green/brightness",
        .max = "/sys/class/leds/led:rgb_green/max_brightness",
      },
      {
        .on  = "/sys/class/leds/led:rgb_blue/blink_delay_on",
        .off = "/sys/class/leds/led:rgb_blue/blink_delay_off",
        .val = "/sys/class/leds/led:rgb_blue/brightness",
        .max = "/sys/class/leds/led:rgb_blue/max_brightness",
      }
    },
    // i9300 (galaxy s3 international)
    {
      {
        .on    = "/sys/class/leds/led_r/delay_on",
        .off   = "/sys/class/leds/led_r/delay_off",
        .val   = "/sys/class/leds/led_r/brightness",
        .max   = "/sys/class/leds/led_r/max_brightness",
        .blink = "/sys/class/leds/led_r/blink",
      },
      {
        .on    = "/sys/class/leds/led_g/delay_on",
        .off   = "/sys/class/leds/led_g/delay_off",
        .val   = "/sys/class/leds/led_g/brightness",
        .max   = "/sys/class/leds/led_g/max_brightness",
        .blink = "/sys/class/leds/led_g/blink",
      },
      {
        .on    = "/sys/class/leds/led_b/delay_on",
        .off   = "/sys/class/leds/led_b/delay_off",
        .val   = "/sys/class/leds/led_b/brightness",
        .max   = "/sys/class/leds/led_b/max_brightness",
        .blink = "/sys/class/leds/led_b/blink",
      }
    },
    // yuga (sony xperia z)
    {
      {
        .val    = "/sys/class/leds/lm3533-red/brightness",
        .maxval = 255,
      },
      {
        .val    = "/sys/class/leds/lm3533-green/brightness",
        .maxval = 255,
      },
      {
        .val    = "/sys/class/leds/lm3533-blue/brightness",
        .maxval = 255,
      },
    },
    // onyx (OnePlus X)
    {
      {
        .val    = "/sys/class/leds/red/brightness",
        .max    = "/sys/class/leds/red/max_brightness",
        .on     = "/sys/class/leds/red/pause_hi",
        .off    = "/sys/class/leds/red/pause_lo",
        .blink  = "/sys/class/leds/red/blink",
      },
      {
        .val    = "/sys/class/leds/green/brightness",
        .max    = "/sys/class/leds/green/max_brightness",
        .on     = "/sys/class/leds/green/pause_hi",
        .off    = "/sys/class/leds/green/pause_lo",
        .blink  = "/sys/class/leds/green/blink",
      },
      {
        .val    = "/sys/class/leds/blue/brightness",
        .max    = "/sys/class/leds/blue/max_brightness",
        .on     = "/sys/class/leds/blue/pause_hi",
        .off    = "/sys/class/leds/blue/pause_lo",
        .blink  = "/sys/class/leds/blue/blink",
      },
    },
  };

  static led_state_vanilla_t state[3];

  bool res = false;

  led_state_vanilla_init(state+0);
  led_state_vanilla_init(state+1);
  led_state_vanilla_init(state+2);

  self->name   = "vanilla";
  self->data   = state;
  self->enable = 0;
  self->blink  = led_control_vanilla_blink_cb;
  self->value  = led_control_vanilla_value_cb;
  self->close  = led_control_vanilla_close_cb;

  for( size_t i = 0; i < G_N_ELEMENTS(paths) ; ++i )
  {
    if( led_state_vanilla_probe(&state[0], &paths[i][0]) &&
        led_state_vanilla_probe(&state[1], &paths[i][1]) &&
        led_state_vanilla_probe(&state[2], &paths[i][2]) )
    {
      res = true;
      break;
    }
  }

  if( !res )
  {
    led_control_close(self);
  }

  return res;
}

/* ------------------------------------------------------------------------- *
 * RGB led control: hammerhead backend
 *
 * Three channels, all of which:
 * - must have 'brightness' control file
 * - must have 'max_brightness' control file
 * - must have 'on_off_ms' blink delay control file
 * - must have 'rgb_start' enable/disable control file
 *
 * Assumptions built into code:
 * - Blinking is always soft, handled by kernel driver / hw.
 * - The sysfs writes will block until change is finished -> Intensity
 *   changes are slow. Breathing from userspace can't be used as it
 *   would constantly block mce mainloop.
 * ------------------------------------------------------------------------- */

static void
led_control_hammerhead_enable_cb(void *data, bool enable)
{
  const led_state_hammerhead_t *state = data;
  led_state_hammerhead_set_enabled(state + 0, enable);
  led_state_hammerhead_set_enabled(state + 1, enable);
  led_state_hammerhead_set_enabled(state + 2, enable);
}

static void
led_control_hammerhead_blink_cb(void *data, int on_ms, int off_ms)
{
  const led_state_hammerhead_t *state = data;
  led_state_hammerhead_set_blink(state + 0, on_ms, off_ms);
  led_state_hammerhead_set_blink(state + 1, on_ms, off_ms);
  led_state_hammerhead_set_blink(state + 2, on_ms, off_ms);
}

static void
led_control_hammerhead_value_cb(void *data, int r, int g, int b)
{
  const led_state_hammerhead_t *state = data;
  led_state_hammerhead_set_value(state + 0, r);
  led_state_hammerhead_set_value(state + 1, g);
  led_state_hammerhead_set_value(state + 2, b);
}

static void
led_control_hammerhead_close_cb(void *data)
{
  led_state_hammerhead_t *state = data;
  led_state_hammerhead_close(state + 0);
  led_state_hammerhead_close(state + 1);
  led_state_hammerhead_close(state + 2);
}

static bool
led_control_hammerhead_probe(led_control_t *self)
{
  /** Sysfs control paths for RGB leds */
  static const led_paths_hammerhead_t paths[][3] =
  {
    // hammerhead (Nexus 5)
    {
      {
        .max    = "/sys/class/leds/red/max_brightness",
        .val    = "/sys/class/leds/red/brightness",
        .on_off = "/sys/class/leds/red/on_off_ms",
        .enable = "/sys/class/leds/red/rgb_start",
      },
      {
        .max    = "/sys/class/leds/green/max_brightness",
        .val    = "/sys/class/leds/green/brightness",
        .on_off = "/sys/class/leds/green/on_off_ms",
        .enable = "/sys/class/leds/green/rgb_start",
      },
      {
        .max    = "/sys/class/leds/blue/max_brightness",
        .val    = "/sys/class/leds/blue/brightness",
        .on_off = "/sys/class/leds/blue/on_off_ms",
        .enable = "/sys/class/leds/blue/rgb_start",
      }
    },
  };

  static led_state_hammerhead_t state[3];

  bool res = false;

  led_state_hammerhead_init(state+0);
  led_state_hammerhead_init(state+1);
  led_state_hammerhead_init(state+2);

  self->name   = "hammerhead";
  self->data   = state;
  self->enable = led_control_hammerhead_enable_cb;
  self->blink  = led_control_hammerhead_blink_cb;
  self->value  = led_control_hammerhead_value_cb;
  self->close  = led_control_hammerhead_close_cb;

  /* Changing led parameters is so slow and consumes so much
   * cpu cycles that we just can't have breathing available */
  self->can_breathe = false;

  for( size_t i = 0; i < G_N_ELEMENTS(paths) ; ++i )
  {
    if( led_state_hammerhead_probe(&state[0], &paths[i][0]) &&
        led_state_hammerhead_probe(&state[1], &paths[i][1]) &&
        led_state_hammerhead_probe(&state[2], &paths[i][2]) )
    {
      res = true;
      break;
    }
  }

  if( !res )
  {
    led_control_close(self);
  }

  return res;
}

/* ------------------------------------------------------------------------- *
 * RGB led control: htcvision backend
 *
 * Two channels (amber and green), both of which:
 * - must have 'brightness' control file
 * - must have 'max_brightness' control file
 * - must have 'blink' enable/disable control file
 *
 * Assumptions built into code:
 * - while there are two channels, kernel and/or hw only allows one of them
 *   to be active -> Map rgb form request from mce to amber/green and try
 *   to minimize color error.
 * ------------------------------------------------------------------------- */

static void
led_control_htcvision_map_color(int r, int g, int b,
                                int *amber, int *green)
{
  /* Only "amber" or "green" color can be used.
   *
   * Assume amber = r:ff g:7f b:00
   *        green = r:00 g:ff b:00
   *
   * Try to choose the one with smaller delta to the
   * requested rgb color by using the 'r':'g' ratio.
   *
   * The 'b' is used for intensity preservation only.
   */

  if( r * 3 < g * 4)
  {
    *amber = 0;
    *green = (g > b) ? g : b;
  }
  else
  {
    *amber = (r > b) ? r : b;
    *green = 0;
  }
}

static void
led_control_htcvision_blink_cb(void *data, int on_ms, int off_ms)
{
  const led_state_htcvision_t *state = data;

  int blink = (on_ms && off_ms);

  led_state_htcvision_set_blink(state + 0, blink);
  led_state_htcvision_set_blink(state + 1, blink);
}

static void
led_control_htcvision_value_cb(void *data, int r, int g, int b)
{
  const led_state_htcvision_t *state = data;

  int amber = 0;
  int green = 0;
  led_control_htcvision_map_color(r, g, b, &amber, &green);

  led_state_htcvision_set_value(state + 0, amber);
  led_state_htcvision_set_value(state + 1, green);
}

static void
led_control_htcvision_close_cb(void *data)
{
  led_state_htcvision_t *state = data;
  led_state_htcvision_close(state + 0);
  led_state_htcvision_close(state + 1);
}

static bool
led_control_htcvision_probe(led_control_t *self)
{
  /** Sysfs control paths for Amber/Green leds */
  static const led_paths_htcvision_t paths[][3] =
  {
    // htc vision, htc ace
    {
      {
        .max   = "/sys/class/leds/amber/max_brightness",
        .val   = "/sys/class/leds/amber/brightness",
        .blink = "/sys/class/leds/amber/blink",
      },
      {
        .max   = "/sys/class/leds/green/max_brightness",
        .val   = "/sys/class/leds/green/brightness",
        .blink = "/sys/class/leds/green/blink",
      },
    },
  };

  static led_state_htcvision_t state[2];

  bool res = false;

  led_state_htcvision_init(state+0);
  led_state_htcvision_init(state+1);

  self->name   = "htcvision";
  self->data   = state;
  self->enable = 0;
  self->blink  = led_control_htcvision_blink_cb;
  self->value  = led_control_htcvision_value_cb;
  self->close  = led_control_htcvision_close_cb;

  /* TODO: check if breathing can be left enabled */
  self->can_breathe = true;

  for( size_t i = 0; i < G_N_ELEMENTS(paths) ; ++i )
  {
    if( led_state_htcvision_probe(&state[0], &paths[i][0]) &&
        led_state_htcvision_probe(&state[1], &paths[i][1]) )
    {
      res = true;
      break;
    }
  }

  if( !res )
  {
    led_control_close(self);
  }

  return res;
}

/* ------------------------------------------------------------------------- *
 * RGB led control: binary backend
 *
 * One channels, which:
 * - must have 'brightness' control file
 *
 * Assumptions built into code:
 * - Using zero brightness disables led, any non-zero value enables it -> If
 *   mce requests "black" rgb value, use brightness of zero - otherwise 255.
 * ------------------------------------------------------------------------- */

static void
led_control_binary_map_color(int r, int g, int b, int *mono)
{
  /* Only binary on/off control is available, use
   * 255 as logical level if nonzero rgb value has
   * been requested.
   */
  *mono = (r || g || b) ? 255 : 0;
}

static void
led_control_binary_value_cb(void *data, int r, int g, int b)
{
  led_state_binary_t *state = data;

  int mono = 0;
  led_control_binary_map_color(r, g, b, &mono);
  led_state_binary_set_value(state + 0, mono);
}

static void
led_control_binary_close_cb(void *data)
{
  led_state_binary_t *state = data;
  led_state_binary_close(state + 0);
}

static bool
led_control_binary_probe(led_control_t *self)
{
  /** Sysfs control paths for binary leds */
  static const led_paths_binary_t paths[][1] =
  {
    // binary
    {
      {
        .val = "/sys/class/leds/button-backlight/brightness",
      },
    },
  };

  static led_state_binary_t state[1];

  bool res = false;

  led_state_binary_init(state+0);

  self->name   = "binary";
  self->data   = state;
  self->enable = 0;
  self->blink  = 0;
  self->value  = led_control_binary_value_cb;
  self->close  = led_control_binary_close_cb;

  /* We can use sw breathing logic to simulate hw blinking */
  self->can_breathe = true;
  self->breath_type = LED_RAMP_HARD_STEP;

  for( size_t i = 0; i < G_N_ELEMENTS(paths) ; ++i )
  {
    if( led_state_binary_probe(&state[0], &paths[i][0]) )
    {
      res = true;
      break;
    }
  }

  if( !res )
  {
    led_control_close(self);
  }

  return res;
}

/* ------------------------------------------------------------------------- *
 * LED control flow and timing logic underneath hybris API
 * ------------------------------------------------------------------------- */

/** Questimate of the duration of the kernel delayed work */
#define LED_CTRL_KERNEL_DELAY 10 // [ms]

/** Minimum delay between breathing steps */
#define LED_CTRL_BREATHING_DELAY 50 // [ms]

/** Maximum number of breathing steps; rise and fall time combined */
#define LED_CTRL_MAX_STEPS 256

/** Minimum number of breathing steps on rise/fall time */
#define LED_CTRL_MIN_STEPS 5

/** Led request parameters */
typedef struct
{
  int  r,g,b;    // color
  int  on,off;   // blink timing
  int  level;    // brightness [0 ... 255]
  bool breathe;  // breathe instead of blinking
} led_request_t;

/** Test for led request blink/breathing timing equality
 */
static bool led_request_has_equal_timing(const led_request_t *self,
                                         const led_request_t *that)
{
    return (self->on  == that->on &&
            self->off == that->off);
}

/** Test for led request equality
 */
static bool led_request_is_equal(const led_request_t *self,
                                 const led_request_t *that)
{
    return (self->r       == that->r  &&
            self->g       == that->g  &&
            self->b       == that->b  &&
            self->on      == that->on &&
            self->off     == that->off &&
            self->level   == that->level &&
            self->breathe == that->breathe);
}

/** Test for active led request
 */
static bool led_request_has_color(const led_request_t *self)
{
    return self->r > 0 || self->g > 0 || self->b > 0;
}

/** Normalize/sanity check requested values
 */
static void led_request_sanitize(led_request_t *self)
{
  int min_period = LED_CTRL_BREATHING_DELAY * LED_CTRL_MIN_STEPS;

  if( !led_request_has_color(self) ) {
    /* blinking/breathing black and black makes no sense */
    self->on  = 0;
    self->off = 0;
    self->breathe = false;
  }
  else if( self->on <= 0 || self->off <= 0) {
    /* both on and off periods must be > 0 for blinking/breathing */
    self->on  = 0;
    self->off = 0;
    self->breathe = false;
  }
  else if( self->on < min_period || self->off < min_period ) {
    /* Whether a pattern should breathe or not is decided at mce side.
     * But, since there are limitations on how often the led intensity
     * can be changed, we must check that the rise/fall times are long
     * enough to allow a reasonable amount of adjustments to be made. */
    self->breathe = false;
  }
}

/** Different styles of led patterns */
typedef enum {
  STYLE_OFF,     // led is off
  STYLE_STATIC,  // led has constant color
  STYLE_BLINK,   // led is blinking with on/off periods
  STYLE_BREATH,  // led is breathing with rise/fall times
} led_style_t;

/** Evaluate request style
 */
static led_style_t led_request_get_style(const led_request_t *self)
{
  if( !led_request_has_color(self) ) {
    return STYLE_OFF;
  }

  if( self->on <= 0 || self->off <= 0 ) {
    return STYLE_STATIC;
  }

  if( self->breathe ) {
    return STYLE_BREATH;
  }

  return STYLE_BLINK;
}

/** Intensity curve for sw breathing */
static struct {
  size_t  step;
  size_t  steps;
  int     delay;
  uint8_t value[LED_CTRL_MAX_STEPS];
} led_ctrl_breathe =
{
  .step  = 0,
  .steps = 0,
  .delay = 0,
};

/** Flag for: controls for RGB leds exist in sysfs */
static bool led_ctrl_uses_sysfs = false;

/** Currently active RGB led state; initialize to invalid color */
static led_request_t led_ctrl_curr =
{
  /* force 1st change to take effect by initializing to invalid color */
  .r       = -1,
  .g       = -1,
  .b       = -1,

  /* not blinking or breathing */
  .on      = 0,
  .off     = 0,
  .breathe = false,

  /* full brightness */
  .level   = 255,
};

static led_control_t led_control;

/** Close all LED sysfs files */
static void led_ctrl_close_sysfs_files(void)
{
  led_control_close(&led_control);
}

/** Open sysfs control files for RGB leds
 *
 * @return true if required control files were available, false otherwise
 */
static bool led_ctrl_probe_sysfs_files(void)
{
  led_control_init(&led_control);

  bool probed = led_control_probe(&led_control);

  /* Note: As there are devices that do not have indicator
   *       led, a ailures to find a suitable backend must
   *       be assumed to be ok and not logged in the default
   *       verbosity level.
   */
  mce_log(LOG_NOTICE, "led sysfs backend: %s",
          probed ? led_control.name : "N/A");

  return probed;
}

/** Change blinking attributes of RGB led */
static void led_ctrl_set_rgb_blink(int on, int off)
{
  led_control_blink(&led_control, on, off);
}

/** Change intensity attributes of RGB led */
static void led_ctrl_set_rgb_value(int r, int g, int b)
{
  led_control_value(&led_control, r, g, b);
}

/** Generate half sine intensity curve for use from breathing timer
 */
static void led_ctrl_generate_ramp_half_sin(int ms_on, int ms_off)
{
  int t = ms_on + ms_off;
  int s = (t + LED_CTRL_MAX_STEPS - 1) / LED_CTRL_MAX_STEPS;

  if( s < LED_CTRL_BREATHING_DELAY ) {
    s = LED_CTRL_BREATHING_DELAY;
  }
  int n = (t + s - 1) / s;

  int steps_on  = (n * ms_on + t / 2) / t;
  int steps_off = n - steps_on;

  const float m_pi_2 = (float)M_PI_2;

  int k = 0;

  for( int i = 0; i < steps_on; ++i ) {
    float a = i * m_pi_2 / steps_on;
    led_ctrl_breathe.value[k++] = (uint8_t)(sinf(a) * 255.0f);
  }
  for( int i = 0; i < steps_off; ++i ) {
    float a = m_pi_2 + i * m_pi_2 / steps_off;
    led_ctrl_breathe.value[k++] = (uint8_t)(sinf(a) * 255.0f);
  }

  led_ctrl_breathe.delay = s;
  led_ctrl_breathe.steps = k;

  mce_log(LOG_DEBUG, "delay=%d, steps_on=%d, steps_off=%d",
          led_ctrl_breathe.delay, steps_on, steps_off);
}

/** Calculate the greatest common divisor of two integer numbers
 */
static int gcd(int a, int b)
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
static int roundup(int val, int range)
{
  int extra = val % range;
  if( extra ) extra = range - extra;
  return val + extra;
}

/** Generate hard step intensity curve for use from breathing timer
 */
static void led_ctrl_generate_ramp_hard_step(int ms_on, int ms_off)
{
  /* Calculate ramp duration - round up given on/off lengths
   * to avoid totally bizarre values that could cause excessive
   * number of timer wakeups.
   */
  ms_on  = roundup(ms_on,  100);
  ms_off = roundup(ms_off, 100);

  int ms_tot = ms_on + ms_off;

  /* Ideally we would want to wake up only to flip the led
   * on/off. But to keep using the existing ramp timer logic,
   * we wake up in pace of the greatest common divisor for on
   * and off periods.
   */
  int ms_step = gcd(ms_on, ms_off);

  if( ms_step < LED_CTRL_BREATHING_DELAY ) {
    ms_step = LED_CTRL_BREATHING_DELAY;
  }

  /* Calculate number of steps we need and make sure it does
   * not exceed the defined maximum value.
   */
  int steps_tot = (ms_tot + ms_step - 1) / ms_step;

  if( steps_tot > LED_CTRL_MAX_STEPS ) {
    steps_tot = LED_CTRL_MAX_STEPS;
    ms_step = (ms_tot + steps_tot - 1) / steps_tot;

    if( ms_step < LED_CTRL_BREATHING_DELAY ) {
      ms_step = LED_CTRL_BREATHING_DELAY;
    }
  }

  int steps_on  = (ms_on + ms_step - 1 ) / ms_step;
  int steps_off = steps_tot - steps_on;

  /* Set intensity value for each step on the ramp.
   */
  int i = 0;

  while( i < steps_on  ) led_ctrl_breathe.value[i++] = 255;
  while( i < steps_tot ) led_ctrl_breathe.value[i++] = 0;

  led_ctrl_breathe.delay = ms_step;
  led_ctrl_breathe.steps = steps_tot;

  mce_log(LOG_DEBUG, "delay=%d, steps_on=%d, steps_off=%d",
          led_ctrl_breathe.delay, steps_on, steps_off);
}

/** Invalidate sw breathing intensity curve
 */
static void led_ctrl_generate_ramp_dummy(void)
{
  led_ctrl_breathe.delay = 0;
  led_ctrl_breathe.steps = 0;
}

/** Generate intensity curve for use from breathing timer
 */
static void led_ctrl_generate_ramp(int ms_on, int ms_off)
{
  switch( led_control_breath_type(&led_control) ) {
  case LED_RAMP_HARD_STEP:
    led_ctrl_generate_ramp_hard_step(ms_on, ms_off);
    break;

  case LED_RAMP_HALF_SINE:
    led_ctrl_generate_ramp_half_sin(ms_on, ms_off);
    break;

  default:
    led_ctrl_generate_ramp_dummy();
    break;
  }
}

/** Timer id for stopping led */
static guint led_ctrl_stop_id = 0;

/** Timer id for breathing/setting led */
static guint led_ctrl_step_id = 0;

/** Timer callback for setting led
 */
static gboolean led_ctrl_static_cb(gpointer aptr)
{
  (void) aptr;

  if( !led_ctrl_step_id ) {
    goto cleanup;
  }

  led_ctrl_step_id = 0;

  // get configured color
  int r = led_ctrl_curr.r;
  int g = led_ctrl_curr.g;
  int b = led_ctrl_curr.b;

  // adjust by brightness level
  int l = led_ctrl_curr.level;

  r = led_util_scale_value(r, l);
  g = led_util_scale_value(g, l);
  b = led_util_scale_value(b, l);

  // set led blinking and color
  led_ctrl_set_rgb_blink(led_ctrl_curr.on, led_ctrl_curr.off);
  led_ctrl_set_rgb_value(r, g, b);

cleanup:
  return FALSE;
}

/** Timer callback for taking a led breathing step
 */
static gboolean led_ctrl_step_cb(gpointer aptr)
{
  (void)aptr;

  if( !led_ctrl_step_id ) {
    goto cleanup;
  }

  if( led_ctrl_breathe.step >= led_ctrl_breathe.steps ) {
    led_ctrl_breathe.step = 0;
  }

  // get configured color
  int r = led_ctrl_curr.r;
  int g = led_ctrl_curr.g;
  int b = led_ctrl_curr.b;

  // adjust by brightness level
  int l = led_ctrl_curr.level;

  r = led_util_scale_value(r, l);
  g = led_util_scale_value(g, l);
  b = led_util_scale_value(b, l);

  // adjust by curve position
  size_t i = led_ctrl_breathe.step++;
  int    v = led_ctrl_breathe.value[i];

  r = led_util_scale_value(r, v);
  g = led_util_scale_value(g, v);
  b = led_util_scale_value(b, v);

  // set led color
  led_ctrl_set_rgb_value(r, g, b);

cleanup:
  return led_ctrl_step_id != 0;
}

static bool reset_blinking = true;

/** Timer callback from stopping/restarting led
 */
static gboolean led_ctrl_stop_cb(gpointer aptr)
{
  (void) aptr;

  if( !led_ctrl_stop_id ) {
    goto cleanup;
  }
  led_ctrl_stop_id = 0;

  if( reset_blinking ) {
    // blinking off - must be followed by rgb set to have an effect
    led_ctrl_set_rgb_blink(0, 0);
  }

  if( !led_request_has_color(&led_ctrl_curr) ) {
    // set rgb to black before returning
    reset_blinking = true;
  }
  else {
    if( led_ctrl_breathe.delay > 0 ) {
      // start breathing timer
      led_ctrl_step_id = g_timeout_add(led_ctrl_breathe.delay,
                                       led_ctrl_step_cb, 0);
    }
    else {
      // set rgb to target after timer delay
      led_ctrl_step_id = g_timeout_add(LED_CTRL_KERNEL_DELAY,
                                       led_ctrl_static_cb, 0);
    }
  }

  if( reset_blinking ) {
    // set rgb to black
    led_ctrl_set_rgb_value(0, 0, 0);
    reset_blinking = false;
  }

cleanup:

  return FALSE;
}

/** Start static/blinking/breathing led
 */
static void
led_ctrl_start(const led_request_t *next)
{
  led_request_t work = *next;

  led_request_sanitize(&work);

  if( led_request_is_equal(&led_ctrl_curr, &work) ) {
    goto cleanup;
  }

  /* Assumption: Before changing the led state, we need to wait a bit
   * for kernel side to finish with last change we made and then possibly
   * reset the blinking status and wait a bit more */
  bool restart = true;

  led_style_t old_style = led_request_get_style(&led_ctrl_curr);
  led_style_t new_style = led_request_get_style(&work);

  /* Exception: When we are already breathing and continue to
   * breathe, the blinking is not in use and the breathing timer
   * is keeping the updates far enough from each other */
  if( old_style == STYLE_BREATH && new_style == STYLE_BREATH &&
      led_request_has_equal_timing(&led_ctrl_curr, &work) ) {
    restart = false;
  }

  /* If only the als-based brightness level changes, we need to
   * adjust the breathing amplitude without affecting the phase.
   * Otherwise assume that the pattern has been changed and the
   * breathing step counter needs to be reset. */
  led_ctrl_curr.level = work.level;
  if( !led_request_is_equal(&led_ctrl_curr, &work) ) {
    led_ctrl_breathe.step = 0;
  }

  led_ctrl_curr = work;

  if( restart ) {
    // stop existing breathing timer
    if( led_ctrl_step_id ) {
      g_source_remove(led_ctrl_step_id), led_ctrl_step_id = 0;
    }

    // re-evaluate breathing constants
    led_ctrl_breathe.delay = 0;
    if( new_style == STYLE_BREATH ) {
      led_ctrl_generate_ramp(work.on, work.off);
    }

    /* Schedule led off after kernel settle timeout; once that
     * is done, new led color/blink/breathing will be started */
    if( !led_ctrl_stop_id ) {
      reset_blinking = (old_style == STYLE_BLINK ||
                        new_style == STYLE_BLINK);
      led_ctrl_stop_id = g_timeout_add(LED_CTRL_KERNEL_DELAY,
                                       led_ctrl_stop_cb, 0);
    }
  }

cleanup:

  return;
}

/** Nanosleep helper
 */
static void led_ctrl_wait_kernel(void)
{
  struct timespec ts = { 0, LED_CTRL_KERNEL_DELAY * 1000000l };
  TEMP_FAILURE_RETRY(nanosleep(&ts, &ts));
}

/* ------------------------------------------------------------------------- *
 * hybris led API
 * ------------------------------------------------------------------------- */

/** Initialize libhybris indicator led device object
 *
 * @return true on success, false on failure
 */
bool mce_hybris_indicator_init(void)
{
  static bool done = false;
  static bool ack  = false;

  if( done ) {
    goto cleanup;
  }

  done = true;

  led_ctrl_uses_sysfs = led_ctrl_probe_sysfs_files();

  if( led_ctrl_uses_sysfs ) {
    /* Use raw sysfs controls */

    /* adjust current state to: color=black */
    led_request_t req = led_ctrl_curr;
    req.r = 0;
    req.g = 0;
    req.b = 0;
    led_ctrl_start(&req);
  }
  else {
    /* Fall back to libhybris */

    if( !mce_hybris_modlights_load() ) {
      goto cleanup;
    }

    mce_light_device_open(mod_lights, LIGHT_ID_NOTIFICATIONS, &dev_indicator);

    if( !dev_indicator ) {
      mce_log(LOG_WARNING, "failed to open indicator led device");
      goto cleanup;
    }
  }

  ack = true;

cleanup:

  mce_log(LOG_DEBUG, "res = %s", ack ? "true" : "false");

  return ack;
}

/** Release libhybris indicator led device object
 */
void mce_hybris_indicator_quit(void)
{
  /* Release libhybris controls */

  if( dev_indicator ) {
    mce_light_device_close(dev_indicator), dev_indicator = 0;
  }

  /* Release sysfs controls */

  if( led_ctrl_uses_sysfs ) {
    // cancel timers
    if( led_ctrl_step_id ) {
      g_source_remove(led_ctrl_step_id), led_ctrl_step_id = 0;
    }
    if( led_ctrl_stop_id ) {
      g_source_remove(led_ctrl_stop_id), led_ctrl_stop_id = 0;
    }

    // allow kernel side to settle down
    led_ctrl_wait_kernel();

    // blink off
    led_ctrl_set_rgb_blink(0, 0);

    // zero brightness
    led_ctrl_set_rgb_value(0, 0, 0);

    // close sysfs files
    led_ctrl_close_sysfs_files();
  }
}

/** Clamp integer values to given range
 *
 * @param lo  minimum value allowed
 * @param hi  maximum value allowed
 * @param val value to clamp
 *
 * @return val clamped to [lo, hi]
 */
static inline int clamp_to_range(int lo, int hi, int val)
{
  return val <= lo ? lo : val <= hi ? val : hi;
}

/** Set indicator led pattern via libhybris
 *
 * @param r     red intensity 0 ... 255
 * @param g     green intensity 0 ... 255
 * @param b     blue intensity 0 ... 255
 * @param ms_on milliseconds to keep the led on, or 0 for no flashing
 * @param ms_on milliseconds to keep the led off, or 0 for no flashing
 *
 * @return true on success, false on failure
 */
bool mce_hybris_indicator_set_pattern(int r, int g, int b,
                                      int ms_on, int ms_off)
{
  bool     ack = false;

  /* Sanitize input values */

  /* Clamp time periods to [0, 60] second range.
   *
   * While periods longer than few seconds might not count as "blinking",
   * we need to leave some slack to allow beacon style patterns with
   * relatively long off periods */
  ms_on  = clamp_to_range(0, 60000, ms_on);
  ms_off = clamp_to_range(0, 60000, ms_off);

  /* Both on and off periods need to be non-zero for the blinking
   * to happen in the first place. And if the periods are too
   * short it starts to look like led failure more than indication
   * of something. */
  if( ms_on < 50 || ms_off < 50 ) {
    ms_on = ms_off = 0;
  }

  /* Clamp rgb values to [0, 255] range */
  r = clamp_to_range(0, 255, r);
  g = clamp_to_range(0, 255, g);
  b = clamp_to_range(0, 255, b);

  /* Use raw sysfs controls if possible */

  if( led_ctrl_uses_sysfs ) {

    /* adjust current state to: color & timing as requested */
    led_request_t req = led_ctrl_curr;
    req.r   = r;
    req.g   = g;
    req.b   = b;
    req.on  = ms_on;
    req.off = ms_off;
    led_ctrl_start(&req);

    ack = true;
    goto cleanup;
  }

  /* Fall back to libhybris API */

  struct light_state_t lst;

  if( !mce_hybris_indicator_init() ) {
    goto cleanup;
  }

  memset(&lst, 0, sizeof lst);

  lst.color          = (0xff << 24) | (r << 16) | (g << 8) | (b << 0);
  lst.brightnessMode = BRIGHTNESS_MODE_USER;

  if( ms_on > 0 && ms_off > 0 ) {
    lst.flashMode    = LIGHT_FLASH_HARDWARE;
    lst.flashOnMS    = ms_on;
    lst.flashOffMS   = ms_off;
  }
  else {
    lst.flashMode    = LIGHT_FLASH_NONE;
    lst.flashOnMS    = 0;
    lst.flashOffMS   = 0;
  }

  if( dev_indicator->set_light(dev_indicator, &lst) < 0 ) {
    goto cleanup;
  }

  ack = true;

cleanup:

  mce_log(LOG_DEBUG, "%s(%d,%d,%d,%d,%d) -> %s", __FUNCTION__,
         r,g,b, ms_on, ms_off , ack ? "success" : "failure");

  return ack;
}

/** Query if currently active led backend can support breathing
 *
 * @return true if breathing can be requested, false otherwise
 */
bool
mce_hybris_indicator_can_breathe(void)
{
  bool ack = false;

  /* Note: We can't know how access via hybris behaves, so err
   *       on the safe side and assume that breathing is not ok
   *       unless we have direct sysfs controls.
   */

  if( led_ctrl_uses_sysfs )
  {
    ack = led_control_can_breathe(&led_control);
  }

  /* The result does not change during runtime of mce, so
   * log only once */

  static bool logged = false;

  if( !logged )
  {
    logged = true;
    mce_log(LOG_DEBUG, "res = %s", ack ? "true" : "false");
  }

  return ack;
}

/** Enable/disable sw breathing
 *
 * @param enable true to enable sw breathing, false to disable
 */
void mce_hybris_indicator_enable_breathing(bool enable)
{
  mce_log(LOG_DEBUG, "enable = %s", enable ? "true" : "false");

  if( !mce_hybris_indicator_can_breathe() ) {
    if( enable ) {
      static bool once = false;
      if( !once ) {
        once = true;
        mce_log(LOG_ERR, "sw breathing was requested even"
                " though it can't be supported");
      }
    }
    goto cleanup;
  }

  /* adjust current state to: breathing as requested */
  led_request_t work = led_ctrl_curr;
  work.breathe = enable;
  led_ctrl_start(&work);

cleanup:
  return;
}

/** Set indicator led brightness
 *
 * @param level 1=minimum, 255=maximum
 *
 * @return true on success, or false on failure
 */
bool mce_hybris_indicator_set_brightness(int level)
{
  mce_log(LOG_DEBUG, "level = %d", level);

  if( !led_ctrl_uses_sysfs ) {
    // no breathing control via hybris api
    goto cleanup;
  }

  /* Clamp brightness values to [1, 255] range */
  level = clamp_to_range(1, 255, level);

  /* adjust current state to: brightness as requested */
  led_request_t work = led_ctrl_curr;
  work.level = level;
  led_ctrl_start(&work);

cleanup:
  /* Note: failure means this function is not available - which is
   * handled at mce side stub. From this plugin we always return true */
  return true;
}

/* ========================================================================= *
 * SENSORS module
 * ========================================================================= */

/** Convenience function for opening sensors device
 *
 * Similar to what we might or might not have available from hardware/sensors.h
 */
static int
mce_sensors_open(const struct hw_module_t* module,
                 struct sensors_poll_device_t** device)
{
  return module->methods->open(module, SENSORS_HARDWARE_POLL,
                               (struct hw_device_t**)device);
}

/** Convenience function for closing sensors device
 *
 * Similar to what we might or might not have available from hardware/sensors.h
 */
static int
mce_sensors_close(struct sensors_poll_device_t* device)
{
  return device->common.close(&device->common);
}

/** Handle for libhybris sensors plugin */
static struct sensors_module_t       *mod_sensors = 0;

/** Pointer to libhybris sensor poll device object */
static struct sensors_poll_device_t  *dev_poll = 0;

/** Array of sensors available via mod_sensors */
static const struct sensor_t         *sensor_lut = 0;

/** Number of sensors available via mod_sensors */
static int                            sensor_cnt = 0;

/** Pointer to libhybris proximity sensor object */
static const struct sensor_t *ps_sensor = 0;

/** Callback for forwarding proximity sensor events */
static mce_hybris_ps_fn       ps_hook   = 0;

/** Pointer to libhybris ambient light sensor object */
static const struct sensor_t *als_sensor = 0;

/** Callback for forwarding ambient light sensor events */
static mce_hybris_als_fn      als_hook   = 0;

/** Helper for locating sensor objects by type
 *
 * @param type SENSOR_TYPE_LIGHT etc
 *
 * @return sensor pointer, or NULL if not available
 */
static const struct sensor_t *mce_hybris_modsensors_get_sensor(int type)
{
  const struct sensor_t *res = 0;

  for( int i = 0; i < sensor_cnt; ++i ) {

    if( sensor_lut[i].type == type ) {
      res = &sensor_lut[i];
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
static bool mce_hybris_modsensors_load(void)
{
  static bool done = false;

  if( done ) goto cleanup;

  done = true;

  {
    const struct hw_module_t *mod = 0;
    hw_get_module(SENSORS_HARDWARE_MODULE_ID, &mod);
    mod_sensors = (struct sensors_module_t *)mod;
  }

  if( !mod_sensors ) {
    mce_log(LOG_WARNING, "failed top open sensors module");
  }
  else {
    mce_log(LOG_DEBUG, "mod_sensors = %p", mod_sensors);
  }

  if( !mod_sensors ) {
    goto cleanup;
  }

  sensor_cnt = mod_sensors->get_sensors_list(mod_sensors, &sensor_lut);

  als_sensor = mce_hybris_modsensors_get_sensor(SENSOR_TYPE_LIGHT);
  ps_sensor  = mce_hybris_modsensors_get_sensor(SENSOR_TYPE_PROXIMITY);

cleanup:

  return mod_sensors != 0;
}

/** Unload libhybris sensors plugin
 */
static void mce_hybris_modsensors_unload(void)
{
  /* cleanup dependencies */
  mce_hybris_sensors_quit();

  /* actually unload the module */
  // FIXME: how to unload libhybris modules?
}

/* ------------------------------------------------------------------------- *
 * poll device
 * ------------------------------------------------------------------------- */

/** Worker thread id */
static pthread_t poll_tid = 0;

/** Worker thread for reading sensor events via blocking libhybris interface
 *
 * Note: no mce_log() calls from this function - they are not thread safe
 *
 * @param aptr (thread parameter, not used)
 */
static void mce_hybris_sensors_thread(void *aptr)
{
  (void)aptr;

  sensors_event_t eve[32];

  while( dev_poll ) {
    /* This blocks until there are events available, or possibly sooner
     * if enabling/disabling sensors changes something. Since we can't
     * guarantee that we ever return from the call, the thread is cancelled
     * asynchronously on cleanup - and any resources possibly reserved by
     * the dev_poll->poll() are lost. */
    int n = dev_poll->poll(dev_poll, eve, G_N_ELEMENTS(eve));

    for( int i = 0; i < n; ++i ) {
      sensors_event_t *e = &eve[i];

      /* Forward data via per sensor callback routines. The callbacks must
       * handle the fact that they get called from the context of the worker
       * thread. */
      switch( e->type ) {
      case SENSOR_TYPE_LIGHT:
        if( als_hook ) {
          als_hook(e->timestamp, e->distance);
        }
        break;
      case SENSOR_TYPE_PROXIMITY:
        if( ps_hook ) {
          ps_hook(e->timestamp, e->light);
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
static bool mce_hybris_sensors_init(void)
{
  static bool done = false;

  if( !done ) {
    done = true;

    if( !mce_hybris_modsensors_load() ) {
      goto cleanup;
    }

    mce_sensors_open(&mod_sensors->common, &dev_poll);

    if( !dev_poll ) {
      mce_log(LOG_WARNING, "failed to open sensor poll device");
    }
    else {
      mce_log(LOG_DEBUG, "dev_poll = %p", dev_poll);

      if( ps_sensor ) {
        dev_poll->activate(dev_poll, ps_sensor->handle, false);
      }
      if( als_sensor ) {
        dev_poll->activate(dev_poll, als_sensor->handle, false);
      }

      poll_tid = mce_hybris_start_thread(mce_hybris_sensors_thread, 0);
    }
  }

cleanup:
  return dev_poll != 0;
}

/** Release libhybris display backlight device object
 *
 * Also:
 * - stops the sensor input worker thread
 * - disables ALS and PS sensor inputs if possible
 */
static void mce_hybris_sensors_quit(void)
{

  if( dev_poll ) {
    /* Looks like there is no nice way to get the thread to return from
     * dev_poll->poll(), so we need to just cancel the thread ... */
    if( poll_tid != 0 ) {
      mce_log(LOG_DEBUG, "stopping worker thread");
      if( pthread_cancel(poll_tid) != 0 ) {
        mce_log(LOG_ERR, "failed to stop worker thread");
      }
      else {
        void *status = 0;
        pthread_join(poll_tid, &status);
        mce_log(LOG_DEBUG, "worker stopped, status = %p", status);
      }
      poll_tid = 0;
    }

    if( ps_sensor ) {
      dev_poll->activate(dev_poll, ps_sensor->handle, false);
    }
    if( als_sensor ) {
      dev_poll->activate(dev_poll, als_sensor->handle, false);
    }

    mce_sensors_close(dev_poll), dev_poll = 0;
  }
}

/* ------------------------------------------------------------------------- *
 * proximity sensor
 * ------------------------------------------------------------------------- */

/** Start using proximity sensor via libhybris
 *
 * @return true on success, false on failure
 */
bool mce_hybris_ps_init(void)
{
  bool res = false;

  if( !mce_hybris_sensors_init() ) {
    goto cleanup;
  }

  if( !ps_sensor ) {
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
void mce_hybris_ps_quit(void)
{
  ps_hook = 0;
}

/** Set proximity sensort input enabled state
 *
 * @param state true to enable input, or false to disable input
 */
bool mce_hybris_ps_set_active(bool state)
{
  bool res = false;

  if( !mce_hybris_ps_init() ) {
    goto cleanup;
  }

  if( dev_poll->activate(dev_poll, ps_sensor->handle, state) < 0 ) {
    goto cleanup;
  }

  res = true;

cleanup:
  return res;
}

/** Set callback function for handling proximity sensor events
 *
 * Note: the callback function will be called from worker thread.
 */
void mce_hybris_ps_set_hook(mce_hybris_ps_fn cb)
{
  ps_hook = cb;
}

/* ------------------------------------------------------------------------- *
 * ambient light sensor
 * ------------------------------------------------------------------------- */

/** Start using ambient light sensor via libhybris
 *
 * @return true on success, false on failure
 */
bool mce_hybris_als_init(void)
{
  bool res = false;

  if( !mce_hybris_sensors_init() ) {
    goto cleanup;
  }

  if( !als_sensor ) {
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
void mce_hybris_als_quit(void)
{
  als_hook = 0;
}

/** Set ambient light sensor input enabled state
 *
 * @param state true to enable input, or false to disable input
 */
bool mce_hybris_als_set_active(bool state)
{
  bool res = false;

  if( !mce_hybris_als_init() ) {
    goto cleanup;
  }

  if( dev_poll->activate(dev_poll, als_sensor->handle, state) < 0 ) {
    goto cleanup;
  }

  res = true;

cleanup:
  return res;
}

/** Set callback function for handling ambient light sensor events
 *
 * Note: the callback function will be called from worker thread.
 */
void mce_hybris_als_set_hook(mce_hybris_als_fn cb)
{
  als_hook = cb;
}

/* ------------------------------------------------------------------------- *
 * common
 * ------------------------------------------------------------------------- */

/** Release all resources allocated by this module */
void mce_hybris_quit(void)
{
  mce_hybris_modfb_unload();
  mce_hybris_modlights_unload();
  mce_hybris_modsensors_unload();
}
