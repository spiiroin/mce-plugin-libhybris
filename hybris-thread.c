/** @file hybris-thread.c
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

#include "hybris-thread.h"
#include "plugin-logging.h"

#include <stdlib.h>

/* ========================================================================= *
 * PROTOTYPES
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * THREAD_GATE
 * ------------------------------------------------------------------------- */

/** Thread start details; used for inserting custom thread setup code
 */
typedef struct
{
  /** Function to call from initialized thread */
  void (*func)(void *);

  /** Parameter to pass to the thread function */
  void  *data;
} thread_gate_t;

static void          *thread_gate_start_cb (void *aptr);

static thread_gate_t *thread_gate_create   (void (*func)(void *), void *data);
static void           thread_gate_delete   (thread_gate_t *self);

/* ------------------------------------------------------------------------- *
 * GENERIC
 * ------------------------------------------------------------------------- */

pthread_t hybris_thread_start (void (*start)(void *), void *arg);
void      hybris_thread_stop  (pthread_t tid);

/* ========================================================================= *
 * DATA
 * ========================================================================= */

/** Mutex used for synchronous worker thread startup */
static pthread_mutex_t hybris_thread_gate_mutex = PTHREAD_MUTEX_INITIALIZER;

/** Condition used for signaling worker thread startup */
static pthread_cond_t  hybris_thread_gate_cond  = PTHREAD_COND_INITIALIZER;

/* ========================================================================= *
 * THREAD_GATE
 * ========================================================================= */

/** Wrapper for starting new worker thread
 *
 * For use from hybris_thread_start().
 *
 * Before the actual thread start routine is called, the
 * new thread is put in to asynchronously cancellabe state
 * and the starter is woken up via condition.
 *
 * @param aptr wrapper data as void pointer
 *
 * @return 0 on thread exit - via pthread_join()
 */
static void *
thread_gate_start_cb(void *aptr)
{
  thread_gate_t *gate = aptr;

  void  (*func)(void*);
  void   *data;

  /* Allow quick and dirty cancellation */
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0);

  /* Tell thread gate we're up and running */
  pthread_mutex_lock(&hybris_thread_gate_mutex);
  pthread_cond_broadcast(&hybris_thread_gate_cond);
  pthread_mutex_unlock(&hybris_thread_gate_mutex);

  /* Collect data we need, release rest */
  func = gate->func;
  data = gate->data;

  thread_gate_delete(gate),gate = 0;

  /* Call the real thread start */
  func(data);

  return 0;
}

/** Construct a thread gate object
 *
 * @param func Thread function
 * @param data Data to pass to the thread function
 *
 * @return gate object, or NULL
 */
static thread_gate_t *
thread_gate_create(void (*func)(void *), void *data)
{
  thread_gate_t *self = calloc(1, sizeof *self);

  if( self ) {
    self->func = func;
    self->data = data;
  }

  return self;
}

/** Destroy a thread gate object
 *
 * @param self Gate object, or NULL
 */
static void
thread_gate_delete(thread_gate_t *self)
{
  if( self ) {
    free(self);
  }
}

/* ========================================================================= *
 * GENERIC
 * ========================================================================= */

/** Helper for starting new worker thread
 *
 * @param start function to call from new thread
 * @param arg   data to pass to start function
 *
 * @return thread id on success, or 0 on error
 */
pthread_t
hybris_thread_start(void (*start)(void *), void* arg)
{
  pthread_t      tid  = 0;
  thread_gate_t *gate = 0;

  if( !(gate = thread_gate_create(start, arg)) ) {
    goto EXIT;
  }

  pthread_mutex_lock(&hybris_thread_gate_mutex);

  if( pthread_create(&tid, 0, thread_gate_start_cb, gate) != 0 ) {
    mce_log(LL_ERR, "could not start worker thread");

    /* content of tid is undefined on failure, force to zero */
    tid = 0;
  }
  else {
    /* wait until thread has had time to start and set
     * up the cancellation parameters */
    mce_log(LL_DEBUG, "waiting worker to start ...");
    pthread_cond_wait(&hybris_thread_gate_cond, &hybris_thread_gate_mutex);
    mce_log(LL_DEBUG, "worker started");

    /* the thread owns the gate now */
    gate = 0;
  }

  pthread_mutex_unlock(&hybris_thread_gate_mutex);

EXIT:

  thread_gate_delete(gate);

  return tid;
}

/** Helper for terminating worker thread
 *
 * @param tid Thread id from hybris_thread_start()
 */
void
hybris_thread_stop(pthread_t tid)
{
  /* Looks like there is no nice way to get the thread to return from
   * hybris_device_sensors_handle->poll(), so we need to just cancel the thread ... */

  if( tid != 0 ) {
    mce_log(LL_DEBUG, "stopping worker thread");
    if( pthread_cancel(tid) != 0 ) {
      mce_log(LL_ERR, "failed to stop worker thread");
    }
    else {
      void *status = 0;
      pthread_join(tid, &status);
      mce_log(LL_DEBUG, "worker stopped, status = %p", status);
    }
  }
}
