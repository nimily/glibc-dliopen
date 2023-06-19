/* Check if the thread created by POSIX timer using SIGEV_THREAD is
   asynchronously cancellable.
   Copyright (C) 2020-2023 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <https://www.gnu.org/licenses/>.  */

#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>

#include <support/check.h>
#include <support/test-driver.h>
#include <support/xthread.h>

#include <sys/syscall.h>

static pthread_barrier_t barrier;
static pthread_t timer_thread;
static bool cancelled = false;

static void
cl (void *arg)
{
  cancelled = true;
  xpthread_barrier_wait (&barrier);  /* 1 */
}

static void
thread_handler (union sigval sv)
{
  timer_thread = pthread_self ();
  pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

  xpthread_barrier_wait (&barrier);  /* 0 */

  pthread_cleanup_push (cl, NULL);
  for (;;);
  pthread_cleanup_pop (0);
}

static int
do_test (void)
{
  struct sigevent sev = { 0 };
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = &thread_handler;

  timer_t timerid;
  TEST_COMPARE (timer_create (CLOCK_REALTIME, &sev, &timerid), 0);

  xpthread_barrier_init (&barrier, NULL, 2);

  struct itimerspec trigger = { 0 };
  trigger.it_value.tv_nsec = 1000000;
  TEST_COMPARE (timer_settime (timerid, 0, &trigger, NULL), 0);

  /* Sync to get the helper thread id.  */
  xpthread_barrier_wait (&barrier);  /* 0 */

  xpthread_cancel (timer_thread);

  /* Sync with the cancellation cleanup handler.  */
  xpthread_barrier_wait (&barrier);  /* 1 */

  TEST_COMPARE (cancelled, true);

  return 0;
}

/* A stall in cancellation is a regression.  */
#include <support/test-driver.c>
