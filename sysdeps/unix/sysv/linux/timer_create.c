/* Copyright (C) 2003-2023 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If
   not, see <https://www.gnu.org/licenses/>.  */

#include <libc-lock.h>
#include "kernel-posix-timers.h"
#include "kernel-posix-cpu-timers.h"
#include <pthreadP.h>
#include <shlib-compat.h>

struct timer_helper_thread_args_t
{
  /* The barrier is used to synchronize the arguments copy from timer_created
     to the timer thread.  */
  pthread_barrier_t b;
  struct sigevent *evp;
};

static void
timer_helper_thread_cleanup (void *arg)
{
  struct pthread *self = THREAD_SELF;
  /* Clear the MSB bit set by timer_delete.  */
  INTERNAL_SYSCALL_CALL (timer_delete, self->timerid & INT_MAX);

  kernel_timer_t timerid = atomic_load_relaxed (&self->timerid);
  atomic_store_relaxed (&self->timerid, timerid | INT_MIN);
}

static void *
timer_helper_thread (void *arg)
{
  struct timer_helper_thread_args_t *args = arg;

  void (*thrfunc) (sigval_t) = args->evp->sigev_notify_function;
  sigval_t sival = args->evp->sigev_value;
  __pthread_barrier_wait (&args->b);

  struct pthread *self = THREAD_SELF;

  while (1)
    {
      siginfo_t si;
      while (__sigwaitinfo (&sigtimer_set, &si) < 0) {};

      if (si.si_code == SI_TIMER)
	{
	  __libc_cleanup_region_start (1, timer_helper_thread_cleanup, NULL);
	  thrfunc (sival);
	  __libc_cleanup_region_end (0);
	}

      /* timer_delete will set the MSB and signal the thread.  */
      if (atomic_load_relaxed (&self->timerid) < 0)
	break;
    }

  timer_helper_thread_cleanup (NULL);

  return NULL;
}

static int
timer_create_sigev_thread (clockid_t syscall_clockid, struct sigevent *evp,
			   timer_t *timerid)
{
  int ret = -1;

  __pthread_cancel_install_handler ();

  pthread_attr_t attr;
  if (evp->sigev_notify_attributes != NULL)
    __pthread_attr_copy (&attr, evp->sigev_notify_attributes);
  else
    __pthread_attr_init (&attr);
  __pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);

  /* Block all signals in the helper thread but SIGSETXID and SIGCANCEL.  */
  sigset_t ss;
  __sigfillset (&ss);
  clear_internal_signals (&ss);
  ret = __pthread_attr_setsigmask_internal (&attr, &ss);
  if (ret != 0)
    goto out;

  struct timer_helper_thread_args_t args;
  __pthread_barrier_init (&args.b, 0, 2);
  args.evp = evp;

  pthread_t th;
  ret = __pthread_create (&th, &attr, timer_helper_thread, &args);
  if (ret != 0)
    {
      __set_errno (ret);
      goto out;
    }

  struct sigevent kevp = {
    .sigev_value.sival_ptr = NULL,
    .sigev_signo = SIGTIMER,
    .sigev_notify = SIGEV_THREAD_ID,
    ._sigev_un = { ._pad = { [0] = ((struct pthread *)th)->tid } }
  };

  kernel_timer_t ktimerid;
  if (INLINE_SYSCALL_CALL (timer_create, syscall_clockid, &kevp, &ktimerid) < 0)
    goto out;
  ((struct pthread *)th)->timerid = ktimerid;

  __pthread_barrier_wait (&args.b);

  if (timerid >= 0)
    *timerid = pthread_to_timerid (th);

  ret = 0;

out:
  if (&attr != evp->sigev_notify_attributes)
    __pthread_attr_destroy (&attr);

  return ret;
}

int
___timer_create (clockid_t clock_id, struct sigevent *evp, timer_t *timerid)
{
  clockid_t syscall_clockid = (clock_id == CLOCK_PROCESS_CPUTIME_ID
			       ? PROCESS_CLOCK
			       : clock_id == CLOCK_THREAD_CPUTIME_ID
			       ? THREAD_CLOCK
			       : clock_id);

  switch (evp != NULL ? evp->sigev_notify : SIGEV_SIGNAL)
    {
    case SIGEV_NONE:
    case SIGEV_SIGNAL:
    case SIGEV_THREAD_ID:
      {
	struct sigevent kevp;
	if (evp == NULL)
	  {
	    kevp.sigev_notify = SIGEV_SIGNAL;
	    kevp.sigev_signo = SIGALRM;
	    kevp.sigev_value.sival_ptr = NULL;
	    evp = &kevp;
	  }

	kernel_timer_t ktimerid;
	if (INLINE_SYSCALL_CALL (timer_create, syscall_clockid, evp,
				 &ktimerid) == -1)
	  return -1;

	*timerid = kernel_timer_to_timerid (ktimerid);
      }
      break;
    case SIGEV_THREAD:
      if (timer_create_sigev_thread (syscall_clockid, evp, timerid) < 0)
	return -1;
      break;
    default:
      __set_errno (EINVAL);
      return -1;
    }

  return 0;
}
versioned_symbol (libc, ___timer_create, timer_create, GLIBC_2_34);
libc_hidden_ver (___timer_create, __timer_create)

#if TIMER_T_WAS_INT_COMPAT
# if OTHER_SHLIB_COMPAT (librt, GLIBC_2_3_3, GLIBC_2_34)
compat_symbol (librt, ___timer_create, timer_create, GLIBC_2_3_3);
# endif

# if OTHER_SHLIB_COMPAT (librt, GLIBC_2_2, GLIBC_2_3_3)
timer_t __timer_compat_list[OLD_TIMER_MAX];

int
__timer_create_old (clockid_t clock_id, struct sigevent *evp, int *timerid)
{
  timer_t newp;

  int res = __timer_create (clock_id, evp, &newp);
  if (res == 0)
    {
      int i;
      for (i = 0; i < OLD_TIMER_MAX; ++i)
	if (__timer_compat_list[i] == NULL
	    && ! atomic_compare_and_exchange_bool_acq (&__timer_compat_list[i],
						       newp, NULL))
	  {
	    *timerid = i;
	    break;
	  }

      if (__glibc_unlikely (i == OLD_TIMER_MAX))
	{
	  /* No free slot.  */
	  __timer_delete (newp);
	  __set_errno (EINVAL);
	  res = -1;
	}
    }

  return res;
}
compat_symbol (librt, __timer_create_old, timer_create, GLIBC_2_2);
# endif /* OTHER_SHLIB_COMPAT */

#else /* !TIMER_T_WAS_INT_COMPAT */
# if OTHER_SHLIB_COMPAT (librt, GLIBC_2_2, GLIBC_2_34)
compat_symbol (librt, ___timer_create, timer_create, GLIBC_2_2);
# endif
#endif /* !TIMER_T_WAS_INT_COMPAT */
