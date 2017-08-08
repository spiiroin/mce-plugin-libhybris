/** @file sysfs-led-main.c
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

#include "sysfs-led-main.h"

#include "sysfs-led-util.h"
#include "sysfs-led-vanilla.h"
#include "sysfs-led-hammerhead.h"
#include "sysfs-led-bacon.h"
#include "sysfs-led-htcvision.h"
#include "sysfs-led-binary.h"
#include "sysfs-led-redgreen.h"
#include "sysfs-led-white.h"

#include "plugin-logging.h"

#include <stdint.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>

#include <glib.h>

/* ========================================================================= *
 * CONSTANTS
 * ========================================================================= */

/** Questimate of the duration of the kernel delayed work */
#define SYSFS_LED_KERNEL_DELAY 10 // [ms]

/** Minimum delay between breathing steps */
#define SYSFS_LED_STEP_DELAY 50 // [ms]

/** Maximum number of breathing steps; rise and fall time combined */
#define SYSFS_LED_MAX_STEPS 256

/** Minimum number of breathing steps on rise/fall time */
#define SYSFS_LED_MIN_STEPS 5

/* ========================================================================= *
 * PROTOTYPES
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * LED_CONTROL
 *
 * Frontend for controlling (assumed to be RGB) led via sysfs
 * ------------------------------------------------------------------------- */

static void        led_control_enable                (led_control_t *self, bool enable);
static void        led_control_blink                 (led_control_t *self, int on_ms, int off_ms);
static void        led_control_value                 (led_control_t *self, int r, int g, int b);
static void        led_control_init                  (led_control_t *self);

static bool        led_control_can_breathe           (const led_control_t *self);
static led_ramp_t  led_control_breath_type           (const led_control_t *self);

static bool        led_control_probe                 (led_control_t *self);
void               led_control_close                 (led_control_t *self);

/* ------------------------------------------------------------------------- *
 * LED_STATE
 *
 * Managing logical state of indicator led
 * ------------------------------------------------------------------------- */

/** Led state
 */
typedef struct
{
  int  r,g,b;    // color
  int  on,off;   // blink timing
  int  level;    // brightness [0 ... 255]
  bool breathe;  // breathe instead of blinking
} led_state_t;

/** Different styles of led patterns
 */
typedef enum
{
  STYLE_OFF,    // led is off
  STYLE_STATIC, // led has constant color
  STYLE_BLINK,  // led is blinking with on/off periods
  STYLE_BREATH, // led is breathing with rise/fall times
} led_style_t;

static bool        led_state_has_equal_timing        (const led_state_t *self, const led_state_t *that);
static bool        led_state_is_equal                (const led_state_t *self, const led_state_t *that);
static bool        led_state_has_color               (const led_state_t *self);
static void        led_state_sanitize                (led_state_t *self);
static led_style_t led_state_get_style               (const led_state_t *self);

/* ------------------------------------------------------------------------- *
 * SYSFS_LED
 *
 * Top level sysfs led functionality used by mce hybris plugin.
 * ------------------------------------------------------------------------- */

static void        sysfs_led_close_files             (void);
static bool        sysfs_led_probe_files             (void);

static void        sysfs_led_set_rgb_blink           (int on, int off);
static void        sysfs_led_set_rgb_value           (int r, int g, int b);

static void        sysfs_led_generate_ramp_half_sin  (int ms_on, int ms_off);
static void        sysfs_led_generate_ramp_hard_step (int ms_on, int ms_off);
static void        sysfs_led_generate_ramp_dummy     (void);
static void        sysfs_led_generate_ramp           (int ms_on, int ms_off);

static gboolean    sysfs_led_static_cb               (gpointer aptr);
static gboolean    sysfs_led_step_cb                 (gpointer aptr);
static gboolean    sysfs_led_stop_cb                 (gpointer aptr);
static void        sysfs_led_start                   (const led_state_t *next);

static void        sysfs_led_wait_kernel             (void);

bool               sysfs_led_init                    (void);
void               sysfs_led_quit                    (void);

bool               sysfs_led_set_pattern             (int r, int g, int b, int ms_on, int ms_off);
bool               sysfs_led_can_breathe             (void);
void               sysfs_led_set_breathing           (bool enable);
void               sysfs_led_set_brightness          (int level);

/* ========================================================================= *
 * LED_CONTROL
 * ========================================================================= */

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

    /* The bacon backend  */
    led_control_bacon_probe,

    /* The vanilla backend requires only 'brightness'
     * control file, but still needs three directories
     * to be present for red, green and blue channels. */
    led_control_vanilla_probe,

    /* The redgreen uses subset of "standard" rgb led
     * control paths, so to avoid false positive matches
     * it must be probed after rgb led controls. */
    led_control_redgreen_probe,

    /* Single control channel with actually working
     * brightness control and max_brightness. */
    led_control_white_probe,

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

/** Set RGB LED enabled/disable
 *
 * @param self   control object
 * @param enable true for enable, or false for disable
 */

void
led_control_close(led_control_t *self)
{
  if( self->close )
  {
    self->close(self->data);
  }
  led_control_init(self);
}

/* ========================================================================= *
 * LED_STATE
 * ========================================================================= */

/** Test for led request blink/breathing timing equality
 */
static bool
led_state_has_equal_timing(const led_state_t *self, const led_state_t *that)
{
  return (self->on  == that->on &&
          self->off == that->off);
}

/** Test for led request equality
 */
static bool
led_state_is_equal(const led_state_t *self, const led_state_t *that)
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
static bool
led_state_has_color(const led_state_t *self)
{
  return self->r > 0 || self->g > 0 || self->b > 0;
}

/** Normalize/sanity check requested values
 */
static void
led_state_sanitize(led_state_t *self)
{
  int min_period = SYSFS_LED_STEP_DELAY * SYSFS_LED_MIN_STEPS;

  if( !led_state_has_color(self) ) {
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

/** Evaluate request style
 */
static led_style_t
led_state_get_style(const led_state_t *self)
{
  if( !led_state_has_color(self) ) {
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

/* ========================================================================= *
 * SYSFS_LED
 * ========================================================================= */

/** Currently used intensity curve for sw breathing */
static struct {
  size_t  step;
  size_t  steps;
  int     delay;
  uint8_t value[SYSFS_LED_MAX_STEPS];
} sysfs_led_breathe =
{
  .step  = 0,
  .steps = 0,
  .delay = 0,
};

/** Currently active RGB led state; initialize to invalid color */
static led_state_t sysfs_led_curr =
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

/** Close all LED sysfs files
 */
static void
sysfs_led_close_files(void)
{
  led_control_close(&led_control);
}

/** Open sysfs control files for RGB leds
 *
 * @return true if required control files were available, false otherwise
 */
static bool
sysfs_led_probe_files(void)
{
  led_control_init(&led_control);

  bool probed = led_control_probe(&led_control);

  /* Note: As there are devices that do not have indicator
   *       led, a ailures to find a suitable backend must
   *       be assumed to be ok and not logged in the default
   *       verbosity level.
   */
  mce_log(LL_NOTICE, "led sysfs backend: %s",
          probed ? led_control.name : "N/A");

  return probed;
}

/** Change blinking attributes of RGB led
 */
static void
sysfs_led_set_rgb_blink(int on, int off)
{
  mce_log(LOG_DEBUG, "on_ms = %d, off_ms = %d", on, off);
  led_control_blink(&led_control, on, off);
}

/** Change intensity attributes of RGB led
 */
static void
sysfs_led_set_rgb_value(int r, int g, int b)
{
  mce_log(LOG_DEBUG, "rgb = %d %d %d", r, g, b);
  led_control_value(&led_control, r, g, b);
}

/** Generate half sine intensity curve for use from breathing timer
 */
static void
sysfs_led_generate_ramp_half_sin(int ms_on, int ms_off)
{
  int t = ms_on + ms_off;
  int s = (t + SYSFS_LED_MAX_STEPS - 1) / SYSFS_LED_MAX_STEPS;

  if( s < SYSFS_LED_STEP_DELAY ) {
    s = SYSFS_LED_STEP_DELAY;
  }
  int n = (t + s - 1) / s;

  int steps_on  = (n * ms_on + t / 2) / t;
  int steps_off = n - steps_on;

  const float m_pi_2 = (float)M_PI_2;

  int k = 0;

  for( int i = 0; i < steps_on; ++i ) {
    float a = i * m_pi_2 / steps_on;
    sysfs_led_breathe.value[k++] = (uint8_t)(sinf(a) * 255.0f);
  }
  for( int i = 0; i < steps_off; ++i ) {
    float a = m_pi_2 + i * m_pi_2 / steps_off;
    sysfs_led_breathe.value[k++] = (uint8_t)(sinf(a) * 255.0f);
  }

  sysfs_led_breathe.delay = s;
  sysfs_led_breathe.steps = k;

  mce_log(LL_DEBUG, "delay=%d, steps_on=%d, steps_off=%d",
          sysfs_led_breathe.delay, steps_on, steps_off);
}

/** Generate hard step intensity curve for use from breathing timer
 */
static void
sysfs_led_generate_ramp_hard_step(int ms_on, int ms_off)
{
  /* Calculate ramp duration - round up given on/off lengths
   * to avoid totally bizarre values that could cause excessive
   * number of timer wakeups.
   */
  ms_on  = led_util_roundup(ms_on,  100);
  ms_off = led_util_roundup(ms_off, 100);

  int ms_tot = ms_on + ms_off;

  /* Ideally we would want to wake up only to flip the led
   * on/off. But to keep using the existing ramp timer logic,
   * we wake up in pace of the greatest common divisor for on
   * and off periods.
   */
  int ms_step = led_util_gcd(ms_on, ms_off);

  if( ms_step < SYSFS_LED_STEP_DELAY ) {
    ms_step = SYSFS_LED_STEP_DELAY;
  }

  /* Calculate number of steps we need and make sure it does
   * not exceed the defined maximum value.
   */
  int steps_tot = (ms_tot + ms_step - 1) / ms_step;

  if( steps_tot > SYSFS_LED_MAX_STEPS ) {
    steps_tot = SYSFS_LED_MAX_STEPS;
    ms_step = (ms_tot + steps_tot - 1) / steps_tot;

    if( ms_step < SYSFS_LED_STEP_DELAY ) {
      ms_step = SYSFS_LED_STEP_DELAY;
    }
  }

  int steps_on  = (ms_on + ms_step - 1 ) / ms_step;
  int steps_off = steps_tot - steps_on;

  /* Set intensity value for each step on the ramp.
   */
  int i = 0;

  while( i < steps_on  ) sysfs_led_breathe.value[i++] = 255;
  while( i < steps_tot ) sysfs_led_breathe.value[i++] = 0;

  sysfs_led_breathe.delay = ms_step;
  sysfs_led_breathe.steps = steps_tot;

  mce_log(LL_DEBUG, "delay=%d, steps_on=%d, steps_off=%d",
          sysfs_led_breathe.delay, steps_on, steps_off);
}

/** Invalidate sw breathing intensity curve
 */
static void
sysfs_led_generate_ramp_dummy(void)
{
  sysfs_led_breathe.delay = 0;
  sysfs_led_breathe.steps = 0;
}

/** Generate intensity curve for use from breathing timer
 */
static void
sysfs_led_generate_ramp(int ms_on, int ms_off)
{
  switch( led_control_breath_type(&led_control) ) {
  case LED_RAMP_HARD_STEP:
    sysfs_led_generate_ramp_hard_step(ms_on, ms_off);
    break;

  case LED_RAMP_HALF_SINE:
    sysfs_led_generate_ramp_half_sin(ms_on, ms_off);
    break;

  default:
    sysfs_led_generate_ramp_dummy();
    break;
  }
}

/** Timer id for stopping led */
static guint sysfs_led_stop_id = 0;

/** Timer id for breathing/setting led */
static guint sysfs_led_step_id = 0;

/** Timer callback for setting led
 */
static gboolean
sysfs_led_static_cb(gpointer aptr)
{
  (void) aptr;

  if( !sysfs_led_step_id ) {
    goto cleanup;
  }

  sysfs_led_step_id = 0;

  // get configured color
  int r = sysfs_led_curr.r;
  int g = sysfs_led_curr.g;
  int b = sysfs_led_curr.b;

  // adjust by brightness level
  int l = sysfs_led_curr.level;

  r = led_util_scale_value(r, l);
  g = led_util_scale_value(g, l);
  b = led_util_scale_value(b, l);

  // set led blinking and color
  sysfs_led_set_rgb_blink(sysfs_led_curr.on, sysfs_led_curr.off);
  sysfs_led_set_rgb_value(r, g, b);

cleanup:

  return FALSE;
}

/** Timer callback for taking a led breathing step
 */
static gboolean
sysfs_led_step_cb(gpointer aptr)
{
  (void)aptr;

  if( !sysfs_led_step_id ) {
    goto cleanup;
  }

  if( sysfs_led_breathe.step >= sysfs_led_breathe.steps ) {
    sysfs_led_breathe.step = 0;
  }

  // get configured color
  int r = sysfs_led_curr.r;
  int g = sysfs_led_curr.g;
  int b = sysfs_led_curr.b;

  // adjust by brightness level
  int l = sysfs_led_curr.level;

  r = led_util_scale_value(r, l);
  g = led_util_scale_value(g, l);
  b = led_util_scale_value(b, l);

  // adjust by curve position
  size_t i = sysfs_led_breathe.step++;
  int    v = sysfs_led_breathe.value[i];

  r = led_util_scale_value(r, v);
  g = led_util_scale_value(g, v);
  b = led_util_scale_value(b, v);

  // set led color
  sysfs_led_set_rgb_value(r, g, b);

cleanup:

  return sysfs_led_step_id != 0;
}

static bool sysfs_led_reset_blinking = true;

/** Timer callback from stopping/restarting led
 */
static gboolean
sysfs_led_stop_cb(gpointer aptr)
{
  (void) aptr;

  if( !sysfs_led_stop_id ) {
    goto cleanup;
  }
  sysfs_led_stop_id = 0;

  if( sysfs_led_reset_blinking ) {
    // blinking off - must be followed by rgb set to have an effect
    sysfs_led_set_rgb_blink(0, 0);
  }

  if( !led_state_has_color(&sysfs_led_curr) ) {
    // set rgb to black before returning
    sysfs_led_reset_blinking = true;
  }
  else {
    if( sysfs_led_breathe.delay > 0 ) {
      // start breathing timer
      sysfs_led_step_id = g_timeout_add(sysfs_led_breathe.delay,
                                        sysfs_led_step_cb, 0);
    }
    else {
      // set rgb to target after timer delay
      sysfs_led_step_id = g_timeout_add(SYSFS_LED_KERNEL_DELAY,
                                        sysfs_led_static_cb, 0);
    }
  }

  if( sysfs_led_reset_blinking ) {
    // set rgb to black
    sysfs_led_set_rgb_value(0, 0, 0);
    sysfs_led_reset_blinking = false;
  }

cleanup:

  return FALSE;
}

/** Start static/blinking/breathing led
 */
static void
sysfs_led_start(const led_state_t *next)
{
  led_state_t work = *next;

  led_state_sanitize(&work);

  if( led_state_is_equal(&sysfs_led_curr, &work) ) {
    goto cleanup;
  }

  /* Assumption: Before changing the led state, we need to wait a bit
   * for kernel side to finish with last change we made and then possibly
   * reset the blinking status and wait a bit more */
  bool restart = true;

  led_style_t old_style = led_state_get_style(&sysfs_led_curr);
  led_style_t new_style = led_state_get_style(&work);

  /* Exception: When we are already breathing and continue to
   * breathe, the blinking is not in use and the breathing timer
   * is keeping the updates far enough from each other */
  if( old_style == STYLE_BREATH && new_style == STYLE_BREATH &&
      led_state_has_equal_timing(&sysfs_led_curr, &work) ) {
    restart = false;
  }

  /* If only the als-based brightness level changes, we need to
   * adjust the breathing amplitude without affecting the phase.
   * Otherwise assume that the pattern has been changed and the
   * breathing step counter needs to be reset. */
  sysfs_led_curr.level = work.level;
  if( !led_state_is_equal(&sysfs_led_curr, &work) ) {
    sysfs_led_breathe.step = 0;
  }

  sysfs_led_curr = work;

  if( restart ) {
    // stop existing breathing timer
    if( sysfs_led_step_id ) {
      g_source_remove(sysfs_led_step_id), sysfs_led_step_id = 0;
    }

    // re-evaluate breathing constants
    sysfs_led_breathe.delay = 0;
    if( new_style == STYLE_BREATH ) {
      sysfs_led_generate_ramp(work.on, work.off);
    }

    /* Schedule led off after kernel settle timeout; once that
     * is done, new led color/blink/breathing will be started */
    if( !sysfs_led_stop_id ) {
      sysfs_led_reset_blinking = (old_style == STYLE_BLINK ||
                                  new_style == STYLE_BLINK);
      sysfs_led_stop_id = g_timeout_add(SYSFS_LED_KERNEL_DELAY,
                                        sysfs_led_stop_cb, 0);
    }
  }

cleanup:

  return;
}

/** Nanosleep helper
 */
static void
sysfs_led_wait_kernel(void)
{
  struct timespec ts = { 0, SYSFS_LED_KERNEL_DELAY * 1000000l };
  TEMP_FAILURE_RETRY(nanosleep(&ts, &ts));
}

bool
sysfs_led_init(void)
{
  bool ack = false;

  if( !sysfs_led_probe_files() ) {
    goto cleanup;
  }

  /* adjust current state to: color=black */
  led_state_t req = sysfs_led_curr;
  req.r = 0;
  req.g = 0;
  req.b = 0;
  sysfs_led_start(&req);

  ack = true;

cleanup:

  return ack;
}

void
sysfs_led_quit(void)
{
  // cancel timers
  if( sysfs_led_step_id ) {
    g_source_remove(sysfs_led_step_id), sysfs_led_step_id = 0;
  }
  if( sysfs_led_stop_id ) {
    g_source_remove(sysfs_led_stop_id), sysfs_led_stop_id = 0;
  }

  // allow kernel side to settle down
  sysfs_led_wait_kernel();

  // blink off
  sysfs_led_set_rgb_blink(0, 0);

  // zero brightness
  sysfs_led_set_rgb_value(0, 0, 0);

  // close sysfs files
  sysfs_led_close_files();
}

bool
sysfs_led_set_pattern(int r, int g, int b,
                      int ms_on, int ms_off)
{
  /* adjust current state to: color & timing as requested */
  led_state_t req = sysfs_led_curr;
  req.r   = r;
  req.g   = g;
  req.b   = b;
  req.on  = ms_on;
  req.off = ms_off;
  sysfs_led_start(&req);

  return true;
}

bool
sysfs_led_can_breathe(void)
{
  return led_control_can_breathe(&led_control);
}

void
sysfs_led_set_breathing(bool enable)
{
  if( sysfs_led_can_breathe() ) {
    /* adjust current state to: breathing as requested */
    led_state_t work = sysfs_led_curr;
    work.breathe = enable;
    sysfs_led_start(&work);
  }
}

void
sysfs_led_set_brightness(int level)
{
  /* adjust current state to: brightness as requested */
  led_state_t work = sysfs_led_curr;
  work.level = level;
  sysfs_led_start(&work);
}
