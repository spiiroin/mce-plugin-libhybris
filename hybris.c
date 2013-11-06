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
#include <errno.h>

#include <glib.h>

#include <android/system/window.h>
#include <android/hardware/lights.h>
#include <android/hardware/fb.h>
#include <android/hardware/sensors.h>

/** Helper to get number of elements in statically allocated array */
#define numof(a) (sizeof(a)/sizeof*(a))

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
		     struct framebuffer_device_t** device) {
  return module->methods->open(module, GRALLOC_HARDWARE_FB0,
			       (struct hw_device_t**)device);
}

/** Convenience function for closing frame buffer device
 *
 * Similar to what we might or might not have available from hardware/fb.h
 */
static int
mce_framebuffer_close(struct framebuffer_device_t* device) {
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

/* ------------------------------------------------------------------------- *
 * display backlight device
 * ------------------------------------------------------------------------- */

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

/* ------------------------------------------------------------------------- *
 * keypad backlight device
 * ------------------------------------------------------------------------- */

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

/* ------------------------------------------------------------------------- *
 * indicator led device
 * ------------------------------------------------------------------------- */

/** Read number from file */
static int read_number(const char *path)
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

/** Sysfs control paths for a led */
typedef struct
{
  const char *on;  // W
  const char *off; // W
  const char *val; // W
  const char *max; // R
} led_paths_t;

/** Sysfs state for a led */
typedef struct
{
  int fd_on;
  int fd_off;
  int fd_val;
  int maxval;
} led_state_t;

/** Set LED brightness
 *
 * @param self led state
 * @param val  brightness in 0 ... 255 range
 */
static void led_state_set_value(const led_state_t *self, int val)
{
  // transform and clamp from [0 ... 255] to [0 ... maxval]
  val = val * self->maxval / 255;
  if( val > self->maxval ) val = self->maxval;
  if( val < 0 ) val = 0;

  dprintf(self->fd_val, "%d", val);
}

/** Set LED blinking period
 *
 * If both on and off are greater than zero, then the PWM generator
 * is used to full intensity blinking. Otherwise it is used for
 * adjusting the LED brightness.
 *
 * @param self led state
 * @param on   milliseconds on
 * @param off  milliseconds off
 */
static void led_state_set_blink(const led_state_t *self, int on, int off)
{
  dprintf(self->fd_on,  "%d", on);
  dprintf(self->fd_off, "%d", off);
}

/** Clean up led state
 *
 * @param self led state
 */
static void led_state_quit(led_state_t *self)
{
  if( self->fd_on  != -1 ) close(self->fd_on),  self->fd_on  = -1;
  if( self->fd_off != -1 ) close(self->fd_off), self->fd_off = -1;
  if( self->fd_val != -1 ) close(self->fd_val), self->fd_val = -1;
  self->maxval = 255;
}

/** Initialize led state
 *
 * @param self led state
 * @param conf led config
 *
 * @return true if required control files were available, false otherwise
 */
static bool led_state_init(led_state_t *self, const led_paths_t *conf)
{
  bool success = false;

  if( (self->maxval = read_number(conf->max)) <= 0 )
  {
    goto cleanup;
  }
  if( (self->fd_on = open(conf->on, O_WRONLY|O_APPEND)) == -1 )
  {
    goto cleanup;
  }
  if( (self->fd_off = open(conf->off, O_WRONLY|O_APPEND)) == -1 )
  {
    goto cleanup;
  }
  if( (self->fd_val = open(conf->val, O_WRONLY|O_APPEND)) == -1 )
  {
    goto cleanup;
  }

  success = true;

cleanup:
  return success;
}

#define LED_PFIX "/sys/class/leds/led:rgb_"

/** Sysfs control paths for RGB leds */
static const led_paths_t led_paths[3] =
{
  {
    .on  = LED_PFIX"red/blink_delay_on",
    .off = LED_PFIX"red/blink_delay_off",
    .val = LED_PFIX"red/brightness",
    .max = LED_PFIX"red/max_brightness",
  },
  {
    .on  = LED_PFIX"green/blink_delay_on",
    .off = LED_PFIX"green/blink_delay_off",
    .val = LED_PFIX"green/brightness",
    .max = LED_PFIX"green/max_brightness",
  },
  {
    .on  = LED_PFIX"blue/blink_delay_on",
    .off = LED_PFIX"blue/blink_delay_off",
    .val = LED_PFIX"blue/brightness",
    .max = LED_PFIX"blue/max_brightness",
  }
};

/** Sysfs state data for RGB leds */
static led_state_t led_states[3] =
{
  {
    .fd_on  = -1,
    .fd_off = -1,
    .fd_val = -1,
    .maxval = 255,
  },
  {
    .fd_on  = -1,
    .fd_off = -1,
    .fd_val = -1,
    .maxval = 255,
  },
  {
    .fd_on  = -1,
    .fd_off = -1,
    .fd_val = -1,
    .maxval = 255,
  }
};

/** Flag for: controls for RGB leds exist in sysfs */
static bool led_sysfs = false;

/** Close all LED sysfs files */
static void led_flush(void)
{
  for( int i = 0; i < 3; ++i )
  {
    led_state_quit(led_states + i);
  }
}

/** Open sysfs control files for RGB leds
 *
 * @return true if required control files were available, false otherwise
 */
static bool led_probe(void)
{
  bool res = false;

  for( int i = 0; i < 3; ++i )
  {
    if( !led_state_init(led_states + i, led_paths + i) )
    {
      goto cleanup;
    }
  }

  res = true;

cleanup:

  if( !res )
  {
    led_flush();
  }

  return res;
}

/** Change color and blinking attributes of a LED */
static void led_control_blink(int chn, int on, int off)
{
  led_state_set_blink(led_states + chn, on, off);
}

/** Change color and blinking attributes of a LED */
static void led_control_value(int chn, int val)
{
  led_state_set_value(led_states + chn, val);
}

/** Questimate of the duration of the kernel delayed work */
#define LED_QUEUE_DELAY 10 // [ms]

/** State data for led reprogramming "queue" */
typedef struct
{
  int r,g,b;
  int on,off;

  guint id;
  bool  need_reset;
  bool  need_set;

} led_queue_t;

/** Set led to queued ON state if needed
 *
 * @param self led queue object
 *
 * @return true if led was programmed, false otherwise
 */
static bool led_queue_set(led_queue_t *self)
{
  bool done;

  if( (done = self->need_set) )
  {
    self->need_set = false;

    if( self->r || self->g || self->b )
    {
      mce_log(LOG_DEBUG, "LED ON: %02x %02x %02x - %d %d",
	      self->r, self->g, self->b,
	      self->on, self->off);

      // blink setting
      if( self->r ) led_control_blink(0, self->on, self->off);
      if( self->g ) led_control_blink(1, self->on, self->off);
      if( self->b ) led_control_blink(2, self->on, self->off);

      // specify color
      if( self->r ) led_control_value(0, self->r);
      if( self->g ) led_control_value(1, self->g);
      if( self->b ) led_control_value(2, self->b);
    }
    else
    {
      // allow "set to off" cycles to end faster
      done = false;
    }
  }

  return done;
}

/** Set led to OFF state if needed
 *
 * @param self led queue object
 *
 * @return true if led was programmed, false otherwise
 */
static bool led_queue_reset(led_queue_t *self)
{
  bool done;

  if( (done = self->need_reset) )
  {
    self->need_reset = false;

    mce_log(LOG_DEBUG, "LED OFF");

    // blink off
    led_control_blink(0, 0, 0);
    led_control_blink(1, 0, 0);
    led_control_blink(2, 0, 0);

    // zero brightness
    led_control_value(0, 0);
    led_control_value(1, 0);
    led_control_value(2, 0);
  }

  return done;
}

/** Timer callback for processing led queue
 *
 * @param aptr led queue object as void pointer
 *
 * @return TRUE to keep the timer alive, FALSE to stop it
 */
static gboolean led_queue_cb(gpointer aptr)
{
  led_queue_t *self = aptr;

  if( led_queue_reset(self) )
  {
    return TRUE;
  }

  if( led_queue_set(self) )
  {
    return TRUE;
  }

  self->need_reset = true;

  mce_log(LOG_DEBUG, "cycle ended");

  return self->id = 0, FALSE;
}

/** Queue next led ON state
 *
 * Uses timer based state machine to make sure that writes to
 * led brightness sysfs files are at least LED_QUEUE_DELAY ms
 * apart from each other.
 *
 * @param self   led queue object
 * @param r      red value 0 ... 255
 * @param g      green value 0 ... 255
 * @param b      blue value 0 ... 255
 * @param ms_on  on period, or 0 for no blinking
 * @param ms_off off period, or 0 for no blinking
 */
static void
led_queue_schedule(led_queue_t *self,
		   int r, int g, int b,
		   int ms_on, int ms_off)
{
  self->r   = r;
  self->g   = g;
  self->b   = b;
  self->on  = ms_on;
  self->off = ms_off;

  if( self->id )
  {
    /* The queue timer is working on a reset+set cycle */
    if( self->need_set )
    {
      /* We just changed the current cycle before it
       * reached re-program the led stage -> nothing to do */
      mce_log(LOG_DEBUG, "cycle hijacked");
    }
    else
    {
      /* The current cycle is finished -> restart it */
      self->need_reset = true;
      self->need_set = true;
    }
  }
  else
  {
    /* The queue timer is no longer active, so we can reset
     * without delay if needed */
    led_queue_reset(self);

    /* The set goes always via timer cycle - just to keep
     * things relatively simple */
    self->need_set = true;
    self->id = g_timeout_add(LED_QUEUE_DELAY, led_queue_cb, self);
    mce_log(LOG_DEBUG, "cycle started");
  }
}

/** Nanosleep helper
 */
static void led_queue_delay(void)
{
  struct timespec ts = { 0, LED_QUEUE_DELAY * 1000000l };
  TEMP_FAILURE_RETRY(nanosleep(&ts, &ts));
}

/** Cleanup led programming queue
 *
 * @param self  led queue object
 */
static void led_queue_quit(led_queue_t *self)
{
  // stop timer
  if( self->id )
  {
    g_source_remove(self->id), self->id = 0;
    self->need_reset = true;
  }

  // delay, then leds off
  if( self->need_reset ) {
    led_queue_delay();
    led_queue_reset(self);
  }
}

/** Initialize led programming queue
 *
 * @param self  led queue object
 */
static void led_queue_init(led_queue_t *self)
{
  self->r   = 0; // leds off
  self->g   = 0;
  self->b   = 0;

  self->on  = 0; // no blinking
  self->off = 0;

  self->id  = 0; // timer not active

  self->need_reset = true;
  self->need_set   = false;
}

/** Led programming queue state */
static led_queue_t led_queue;

/** Initialize libhybris indicator led device object
 *
 * @return true on success, false on failure
 */
bool mce_hybris_indicator_init(void)
{
  static bool done = false;

  if( !done ) {
    done = true;

    if( (led_sysfs = led_probe()) ) {
      led_queue_init(&led_queue);
      return true;
    }

    if( !mce_hybris_modlights_load() ) {
      goto cleanup;
    }

    mce_light_device_open(mod_lights, LIGHT_ID_NOTIFICATIONS, &dev_indicator);

    if( !dev_indicator ) {
      mce_log(LOG_WARNING, "failed to open indicator led device");
    }
    else {
      mce_log(LOG_DEBUG, "%s() -> %p", __FUNCTION__, dev_indicator);
    }
  }

cleanup:
  return dev_indicator != 0;
}

/** Release libhybris indicator led device object
 */
void mce_hybris_indicator_quit(void)
{
  if( dev_indicator ) {
    mce_light_device_close(dev_indicator), dev_indicator = 0;
  }

  if( led_sysfs ) {
    led_queue_quit(&led_queue);
    led_flush();
  }
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
bool mce_hybris_indicator_set_pattern(int r, int g, int b, int ms_on, int ms_off)
{
  bool     ack = false;

  if( led_sysfs ) {
    if( ms_on <= 0 || ms_off <= 0 ) {
      ms_off = ms_on = 0;
    }

    led_queue_schedule(&led_queue, r, g, b, ms_on, ms_off);

    ack = true;
    goto cleanup;
  }

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
    int n = dev_poll->poll(dev_poll, eve, numof(eve));

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
