/*
 * linux/kernel/posix_timers.c
 *
 *
 * 2002-10-15  Posix Clocks & timers 
 *                           by George Anzinger george@mvista.com
 *
 *			     Copyright (C) 2002 2003 by MontaVista Software.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * MontaVista Software | 1237 East Arques Avenue | Sunnyvale | CA 94085 | USA 
 */

/* These are all the functions necessary to implement
 * POSIX clocks & timers
 */
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/time.h>

#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/idr.h>
#include <linux/hrtime.h>
#include <linux/posix-timers.h>
#include <linux/wait.h>
#include <linux/sc_math.h>
#include <asm/div64.h>
#ifndef div_long_long_rem

#define div_long_long_rem(dividend,divisor,remainder) ({ \
		       u64 result = dividend;		\
		       *remainder = do_div(result,divisor); \
		       result; })

#endif
#define CLOCK_REALTIME_RES TICK_NSEC  /* In nano seconds. */

/*
 * Management arrays for POSIX timers.	 Timers are kept in slab memory
 * Timer ids are allocated by an external routine that keeps track of the
 * id and the timer.  The external interface is:
 *
 * void *idr_find(struct idr *idp, int id);           to find timer_id <id>
 * int idr_get_new(struct idr *idp, void *ptr);       to get a new id and
 *                                                    related it to <ptr>
 * void idr_remove(struct idr *idp, int id);          to release <id>
 * void idr_init(struct idr *idp);                    to initialize <idp>
 *                                                    which we supply.
 * The idr_get_new *may* call slab for more memory so it must not be
 * called under a spin lock.  Likewise idr_remore may release memory
 * (but it may be ok to do this under a lock...).
 * idr_find is just a memory look up and is quite fast.  A -1 return
 * indicates that the requested id does not exist.
 */

/*
 * Lets keep our timers in a slab cache :-)
 */
static kmem_cache_t *posix_timers_cache;
static struct idr posix_timers_id;
static spinlock_t idr_lock = SPIN_LOCK_UNLOCKED;

/*
 * Just because the timer is not in the timer list does NOT mean it is
 * inactive.  It could be in the "fire" routine getting a new expire time.
 */
#define TIMER_INACTIVE 1
#define TIMER_RETRY 1

#ifdef CONFIG_SMP
# define timer_active(tmr) \
		((tmr)->it_timer.entry.prev != (void *)TIMER_INACTIVE)
# define set_timer_inactive(tmr) \
		do { \
			(tmr)->it_timer.entry.prev = (void *)TIMER_INACTIVE; \
		} while (0)
#else
# define timer_active(tmr) BARFY	// error to use outside of SMP
# define set_timer_inactive(tmr) do { } while (0)
#endif


#define REQUEUE_PENDING 1
/*
 * The timer ID is turned into a timer address by idr_find().
 * Verifying a valid ID consists of:
 *
 * a) checking that idr_find() returns other than -1.
 * b) checking that the timer id matches the one in the timer itself.
 * c) that the timer owner is in the callers thread group.
 */

/*
 * CLOCKs: The POSIX standard calls for a couple of clocks and allows us
 *	    to implement others.  This structure defines the various
 *	    clocks and allows the possibility of adding others.	 We
 *	    provide an interface to add clocks to the table and expect
 *	    the "arch" code to add at least one clock that is high
 *	    resolution.	 Here we define the standard CLOCK_REALTIME as a
 *	    1/HZ resolution clock.
 *
 * CPUTIME & THREAD_CPUTIME: We are not, at this time, definding these
 *	    two clocks (and the other process related clocks (Std
 *	    1003.1d-1999).  The way these should be supported, we think,
 *	    is to use large negative numbers for the two clocks that are
 *	    pinned to the executing process and to use -pid for clocks
 *	    pinned to particular pids.	Calls which supported these clock
 *	    ids would split early in the function.
 *
 * RESOLUTION: Clock resolution is used to round up timer and interval
 *	    times, NOT to report clock times, which are reported with as
 *	    much resolution as the system can muster.  In some cases this
 *	    resolution may depend on the underlaying clock hardware and
 *	    may not be quantifiable until run time, and only then is the
 *	    necessary code is written.	The standard says we should say
 *	    something about this issue in the documentation...
 *
 * FUNCTIONS: The CLOCKs structure defines possible functions to handle
 *	    various clock functions.  For clocks that use the standard
 *	    system timer code these entries should be NULL.  This will
 *	    allow dispatch without the overhead of indirect function
 *	    calls.  CLOCKS that depend on other sources (e.g. WWV or GPS)
 *	    must supply functions here, even if the function just returns
 *	    ENOSYS.  The standard POSIX timer management code assumes the
 *	    following: 1.) The k_itimer struct (sched.h) is used for the
 *	    timer.  2.) The list, it_lock, it_clock, it_id and it_process
 *	    fields are not modified by timer code.
 *
 *          At this time all functions EXCEPT clock_nanosleep can be
 *          redirected by the CLOCKS structure.  Clock_nanosleep is in
 *          there, but the code ignors it.
 *
 * Permissions: It is assumed that the clock_settime() function defined
 *	    for each clock will take care of permission checks.	 Some
 *	    clocks may be set able by any user (i.e. local process
 *	    clocks) others not.	 Currently the only set able clock we
 *	    have is CLOCK_REALTIME and its high res counter part, both of
 *	    which we beg off on and pass to do_sys_settimeofday().
 */

static struct k_clock posix_clocks[MAX_CLOCKS];
IF_HIGH_RES(static long arch_res_by_2;)

#define if_clock_do(clock_fun,alt_fun,parms) \
		(!clock_fun) ? alt_fun parms : clock_fun parms

#define p_timer_get(clock,a,b) \
	       	if_clock_do((clock)->timer_get,do_timer_gettime, (a,b))

#define p_nsleep(clock,a,b,c) \
		if_clock_do((clock)->nsleep, do_nsleep, (a,b,c))

#define p_timer_del(clock,a) \
		if_clock_do((clock)->timer_del, do_timer_delete, (a))

void register_posix_clock(int clock_id, struct k_clock *new_clock);
static int do_posix_gettime(struct k_clock *clock, struct timespec *tp);
static u64 do_posix_clock_monotonic_gettime_parts(
	struct timespec *tp, struct timespec *mo, long *sub_jiff);
int do_posix_clock_monotonic_gettime(struct timespec *tp);
int do_posix_clock_monotonic_settime(struct timespec *tp);
static struct k_itimer *lock_timer(timer_t timer_id, unsigned long *flags);
static inline void unlock_timer(struct k_itimer *timr, unsigned long flags);

/*
 * Initialize everything, well, just everything in Posix clocks/timers ;)
 */
static __init int init_posix_timers(void)
{
	struct k_clock clock_realtime = {.res = CLOCK_REALTIME_RES };
	struct k_clock clock_monotonic = {.res = CLOCK_REALTIME_RES,
		.clock_get = do_posix_clock_monotonic_gettime,
		.clock_set = do_posix_clock_monotonic_settime
	};

	register_posix_clock(CLOCK_REALTIME, &clock_realtime);
	register_posix_clock(CLOCK_MONOTONIC, &clock_monotonic);

	posix_timers_cache = kmem_cache_create("posix_timers_cache",
					sizeof (struct k_itimer), 0, 0, 0, 0);
	idr_init(&posix_timers_id);
#ifdef CONFIG_HIGH_RES_TIMERS
	clock_realtime.res = hr_time_resolution;
	register_posix_clock(CLOCK_REALTIME_HR, &clock_realtime);
	clock_monotonic.res = hr_time_resolution;
	register_posix_clock(CLOCK_MONOTONIC_HR, &clock_monotonic);
	arch_res_by_2 = nsec_to_arch_cycle(hr_time_resolution >> 1);
#endif

#ifdef	final_clock_init
	final_clock_init();	/* defined as needed by arch header file */
#endif

	return 0;
}

__initcall(init_posix_timers);
#ifndef CONFIG_HIGH_RES_TIMERS
static void tstojiffie(struct timespec *tp, int res, u64 *jiff)
{
	struct timespec tv = *tp;
	tv.tv_nsec += res - 1;

	timespec_norm(&tv);

	/*
	 * The scaling constants are defined in <linux/time.h>
	 * The difference between there and here is that we do the
	 * res rounding and compute a 64-bit result (well so does that
	 * but it then throws away the high bits).
  	 */
	*jiff =  (((u64)tv.tv_sec * SEC_CONVERSION) +
		  (((u64)tv.tv_nsec * NSEC_CONVERSION) >> 
		   (NSEC_JIFFIE_SC - SEC_JIFFIE_SC))) >> SEC_JIFFIE_SC;
}
#else
static long tstojiffie(struct timespec *tp, int res, u64 *jiff)
{
	struct timespec tv = *tp;
	u64 raw_jiff;
	unsigned long mask_jiff;
	long rtn;

	tv.tv_nsec += res - 1;

	timespec_norm(&tv);

	/*
	 * This much like the above, except, well you know that bit
	 * we shift off the right end to get jiffies.  Well that is
	 * the sub_jiffie part and here we pick that up and convert
	 * it to arch_cycles.
	 *
	 * Also, we need to be a bit more rigorous about resolution,
	 * See the next line...
	 */
	tv.tv_nsec -= tv.tv_nsec % res;

	raw_jiff =  (((u64)tv.tv_sec * SEC_CONVERSION) +
		     (((u64)tv.tv_nsec * NSEC_CONVERSION) >> 
		(NSEC_JIFFIE_SC - SEC_JIFFIE_SC)));
	*jiff = raw_jiff >> SEC_JIFFIE_SC;

	mask_jiff = raw_jiff & ((1 << SEC_JIFFIE_SC) -1);

	rtn = ((u64)mask_jiff * arch_cycles_per_jiffy) >> SEC_JIFFIE_SC;

	/* 
	 * Clean up any slop in the math around zero.
	 */
	if (rtn < arch_res_by_2)
		return  0;

	if ((rtn + arch_res_by_2) > arch_cycles_per_jiffy) {
		(*jiff)++;
		return  0;
	}
	return rtn;
}
#endif


static void schedule_next_timer(struct k_itimer *timr)
{
	struct now_struct now;

	/* Set up the timer for the next interval (if there is one) */
#ifdef CONFIG_HIGH_RES_TIMERS
	if (!(timr->it_incr | timr->it_sub_incr)) 
	    return;
#else
	if (!(timr->it_incr)) 
	    return;
#endif
	posix_get_now(&now);
	do {
		posix_bump_timer(timr);
	}while (posix_time_before(&timr->it_timer, &now));

	timr->it_overrun_last = timr->it_overrun;
	timr->it_overrun = -1;
	++timr->it_requeue_pending;
	add_timer(&timr->it_timer);
}

/*
 * This function is exported for use by the signal deliver code.  It is
 * called just prior to the info block being released and passes that
 * block to us.  It's function is to update the overrun entry AND to
 * restart the timer.  It should only be called if the timer is to be
 * restarted (i.e. we have flagged this in the sys_private entry of the
 * info block).
 *
 * To protect aginst the timer going away while the interrupt is queued,
 * we require that the it_requeue_pending flag be set.
 */
void do_schedule_next_timer(struct siginfo *info)
{
	struct k_itimer *timr;
	unsigned long flags;

	timr = lock_timer(info->si_tid, &flags);

	if (!timr || timr->it_requeue_pending != info->si_sys_private)
		goto exit;

	schedule_next_timer(timr);
	info->si_overrun = timr->it_overrun_last;
exit:
	if (timr)
		unlock_timer(timr, flags);
}

/*
 * Notify the task and set up the timer for the next expiration (if
 * applicable).  This function requires that the k_itimer structure
 * it_lock is taken.  This code will requeue the timer only if we get
 * either an error return or a flag (ret > 0) from send_seg_info
 * indicating that the signal was either not queued or was queued
 * without an info block.  In this case, we will not get a call back to
 * do_schedule_next_timer() so we do it here.  This should be rare...

 * An interesting problem can occur if, while a signal, and thus a call
 * back is pending, the timer is rearmed, i.e. stopped and restarted.
 * We then need to sort out the call back and do the right thing.  What
 * we do is to put a counter in the info block and match it with the
 * timers copy on the call back.  If they don't match, we just ignore
 * the call back.  The counter is local to the timer and we use odd to
 * indicate a call back is pending.  Note that we do allow the timer to 
 * be deleted while a signal is pending.  The standard says we can
 * allow that signal to be delivered, and we do. 
 */

static void timer_notify_task(struct k_itimer *timr)
{
	struct siginfo info;
	int ret;

	memset(&info, 0, sizeof (info));

	/* Send signal to the process that owns this timer. */
	info.si_signo = timr->it_sigev_signo;
	info.si_errno = 0;
	info.si_code = SI_TIMER;
	info.si_tid = timr->it_id;
	info.si_value = timr->it_sigev_value;
	if ((timr->it_incr  IF_HIGH_RES( | timr->it_sub_incr)))
		info.si_sys_private = ++timr->it_requeue_pending;

	if (timr->it_sigev_notify & SIGEV_THREAD_ID )
		ret = send_sig_info(info.si_signo, &info, timr->it_process);
	else
		ret = send_group_sig_info(info.si_signo, &info, 
					  timr->it_process);
	switch (ret) {

	default:
		/*
		 * Signal was not sent.  May or may not need to
		 * restart the timer.
		 */
		printk(KERN_WARNING "sending signal failed: %d\n", ret);
	case 1:
		/*
		 * signal was not sent because of sig_ignor or,
		 * possibly no queue memory OR will be sent but,
		 * we will not get a call back to restart it AND
		 * it should be restarted.
		 */
		schedule_next_timer(timr);
	case 0:
		/*
		 * all's well new signal queued
		 */
		break;
	}
}

/*
 * This function gets called when a POSIX.1b interval timer expires.  It
 * is used as a callback from the kernel internal timer.  The
 * run_timer_list code ALWAYS calls with interrutps on.
 */
static void posix_timer_fn(unsigned long __data)
{
	struct k_itimer *timr = (struct k_itimer *) __data;
	unsigned long flags;

	spin_lock_irqsave(&timr->it_lock, flags);
 	set_timer_inactive(timr);
	timer_notify_task(timr);
	unlock_timer(timr, flags);
}


static inline struct task_struct * good_sigevent(sigevent_t * event)
{
	struct task_struct *rtn = current;

	if ((event->sigev_notify & SIGEV_THREAD_ID ) &&
		(!(rtn = find_task_by_pid(event->sigev_notify_thread_id)) ||
		 rtn->tgid != current->tgid ||
		 (event->sigev_notify & ~SIGEV_THREAD_ID) != SIGEV_SIGNAL))
		return NULL;

	if (((event->sigev_notify & ~SIGEV_THREAD_ID) != SIGEV_NONE) &&
	    ((unsigned) (event->sigev_signo > _NSIG) || !event->sigev_signo))
		return NULL;

	return rtn;
}

void register_posix_clock(int clock_id, struct k_clock *new_clock)
{
	if ((unsigned) clock_id >= MAX_CLOCKS) {
		printk("POSIX clock register failed for clock_id %d\n",
		       clock_id);
		return;
	}
	posix_clocks[clock_id] = *new_clock;
}

static struct k_itimer * alloc_posix_timer(void)
{
	struct k_itimer *tmr;
	tmr = kmem_cache_alloc(posix_timers_cache, GFP_KERNEL);
	memset(tmr, 0, sizeof (struct k_itimer));

	return tmr;
}

static void release_posix_timer(struct k_itimer *tmr)
{
	if (tmr->it_id != -1) {
		spin_lock_irq(&idr_lock);
		idr_remove(&posix_timers_id, tmr->it_id);
		spin_unlock_irq(&idr_lock);
	}
	kmem_cache_free(posix_timers_cache, tmr);
}

/* Create a POSIX.1b interval timer. */

asmlinkage long
sys_timer_create(clockid_t which_clock,
		 struct sigevent __user *timer_event_spec,
		 timer_t __user * created_timer_id)
{
	int error = 0;
	struct k_itimer *new_timer = NULL;
	timer_t new_timer_id;
	struct task_struct *process = 0;
	sigevent_t event;

	if ((unsigned) which_clock >= MAX_CLOCKS ||
				!posix_clocks[which_clock].res)
		return -EINVAL;

	new_timer = alloc_posix_timer();
	if (unlikely(!new_timer))
		return -EAGAIN;

	spin_lock_init(&new_timer->it_lock);
	do {
		if (unlikely(!idr_pre_get(&posix_timers_id))) {
			error = -EAGAIN;
			new_timer->it_id = (timer_t)-1;
			goto out;
		}
		spin_lock_irq(&idr_lock);
		new_timer_id = (timer_t) idr_get_new(&posix_timers_id,
							(void *) new_timer);
		spin_unlock_irq(&idr_lock);
	} while (unlikely(new_timer_id == -1));

	new_timer->it_id = new_timer_id;
	/*
	 * return the timer_id now.  The next step is hard to
	 * back out if there is an error.
	 */
	if (copy_to_user(created_timer_id,
			 &new_timer_id, sizeof (new_timer_id))) {
		error = -EFAULT;
		goto out;
	}
	if (timer_event_spec) {
		if (copy_from_user(&event, timer_event_spec, sizeof (event))) {
			error = -EFAULT;
			goto out;
		}
		read_lock(&tasklist_lock);
		if ((process = good_sigevent(&event))) {
			/*
			 * We may be setting up this process for another
			 * thread.  It may be exiting.  To catch this
			 * case the we check the PF_EXITING flag.  If
			 * the flag is not set, the task_lock will catch
			 * him before it is too late (in exit_itimers).
			 *
			 * The exec case is a bit more invloved but easy
			 * to code.  If the process is in our thread
			 * group (and it must be or we would not allow
			 * it here) and is doing an exec, it will cause
			 * us to be killed.  In this case it will wait
			 * for us to die which means we can finish this
			 * linkage with our last gasp. I.e. no code :)
			 */
			task_lock(process);
			if (!(process->flags & PF_EXITING)) {
				list_add(&new_timer->list,
					 &process->posix_timers);
				task_unlock(process);
			} else {
				task_unlock(process);
				process = 0;
			}
		}
		read_unlock(&tasklist_lock);
		if (!process) {
			error = -EINVAL;
			goto out;
		}
		new_timer->it_sigev_notify = event.sigev_notify;
		new_timer->it_sigev_signo = event.sigev_signo;
		new_timer->it_sigev_value = event.sigev_value;
	} else {
		new_timer->it_sigev_notify = SIGEV_SIGNAL;
		new_timer->it_sigev_signo = SIGALRM;
		new_timer->it_sigev_value.sival_int = new_timer->it_id;
		process = current;
		task_lock(process);
		list_add(&new_timer->list, &process->posix_timers);
		task_unlock(process);
	}

	new_timer->it_clock = which_clock;
	new_timer->it_incr = 0;
	new_timer->it_overrun = -1;
	init_timer(&new_timer->it_timer);
	new_timer->it_timer.expires = 0;
	new_timer->it_timer.data = (unsigned long) new_timer;
	new_timer->it_timer.function = posix_timer_fn;
	set_timer_inactive(new_timer);

	/*
	 * Once we set the process, it can be found so do it last...
	 */
	new_timer->it_process = process;
out:
	if (error)
		release_posix_timer(new_timer);

	return error;
}

/*
 * good_timespec
 *
 * This function checks the elements of a timespec structure.
 *
 * Arguments:
 * ts	     : Pointer to the timespec structure to check
 *
 * Return value:
 * If a NULL pointer was passed in, or the tv_nsec field was less than 0
 * or greater than NSEC_PER_SEC, or the tv_sec field was less than 0,
 * this function returns 0. Otherwise it returns 1.
 */
static int good_timespec(const struct timespec *ts)
{
	if ((!ts) || (ts->tv_sec < 0) ||
			((unsigned) ts->tv_nsec >= NSEC_PER_SEC))
		return 0;
	return 1;
}

static inline void unlock_timer(struct k_itimer *timr, unsigned long flags)
{
	spin_unlock_irqrestore(&timr->it_lock, flags);
}

/*
 * Locking issues: We need to protect the result of the id look up until
 * we get the timer locked down so it is not deleted under us.  The
 * removal is done under the idr spinlock so we use that here to bridge
 * the find to the timer lock.  To avoid a dead lock, the timer id MUST
 * be release with out holding the timer lock.
 */
static struct k_itimer * lock_timer(timer_t timer_id, unsigned long *flags)
{
	struct k_itimer *timr;
	/*
	 * Watch out here.  We do a irqsave on the idr_lock and pass the
	 * flags part over to the timer lock.  Must not let interrupts in
	 * while we are moving the lock.
	 */

	spin_lock_irqsave(&idr_lock, *flags);
	timr = (struct k_itimer *) idr_find(&posix_timers_id, (int) timer_id);
	if (timr) {
		spin_lock(&timr->it_lock);
		spin_unlock(&idr_lock);

		if ((timr->it_id != timer_id) || !(timr->it_process) ||
				timr->it_process->tgid != current->tgid) {
			unlock_timer(timr, *flags);
			timr = NULL;
		}
	} else
		spin_unlock_irqrestore(&idr_lock, *flags);

	return timr;
}

/*
 * Get the time remaining on a POSIX.1b interval timer.  This function
 * is ALWAYS called with spin_lock_irq on the timer, thus it must not
 * mess with irq.
 *
 * We have a couple of messes to clean up here.  First there is the case
 * of a timer that has a requeue pending.  These timers should appear to
 * be in the timer list with an expiry as if we were to requeue them
 * now.
 *
 * The second issue is the SIGEV_NONE timer which may be active but is
 * not really ever put in the timer list (to save system resources).
 * This timer may be expired, and if so, we will do it here.  Otherwise
 * it is the same as a requeue pending timer WRT to what we should
 * report.
 */
#ifndef CONFIG_HIGH_RES_TIMERS 
#define timeleft expires
#else
#define timeleft (expires | sub_expires)
#endif

void 
do_timer_gettime(struct k_itimer *timr, struct itimerspec *cur_setting)
{
	IF_HIGH_RES(long sub_expires;)
	unsigned long expires;
	struct now_struct now;

	do {
		expires = timr->it_timer.expires;
		IF_HIGH_RES(sub_expires = timr->it_timer.sub_expires;)
	} while ((volatile long) (timr->it_timer.expires) != expires);

	posix_get_now(&now);

	if (timeleft && 
	    ((timr->it_sigev_notify & ~SIGEV_THREAD_ID) == SIGEV_NONE) && 
	    !(timr->it_incr IF_HIGH_RES(| timr->it_sub_incr)) &&
	    posix_time_before(&timr->it_timer, &now))
		IF_HIGH_RES(timr->it_timer.sub_expires = sub_expires =) 
			timr->it_timer.expires = expires = 0;
	if (timeleft) {
		if (timr->it_requeue_pending & REQUEUE_PENDING ||
		    (timr->it_sigev_notify & ~SIGEV_THREAD_ID) == SIGEV_NONE)
			while (posix_time_before(&timr->it_timer, &now))
				posix_bump_timer(timr);
		else
			if (!timer_pending(&timr->it_timer))
				expires = IF_HIGH_RES(sub_expires =) 0;
		if (timeleft) {
			expires -= now.jiffies;
			IF_HIGH_RES(sub_expires -= now.sub_jiffie;)
		}
	}
	jiffies_to_timespec(expires, &cur_setting->it_value);
	jiffies_to_timespec(timr->it_incr, &cur_setting->it_interval);

#ifdef CONFIG_HIGH_RES_TIMERS 
	cur_setting->it_value.tv_nsec +=
		arch_cycle_to_nsec(sub_expires);
	if (cur_setting->it_value.tv_nsec < 0) {
		cur_setting->it_value.tv_nsec += NSEC_PER_SEC;
		cur_setting->it_value.tv_sec--;}
	if ((cur_setting->it_value.tv_nsec - NSEC_PER_SEC) >= 0) {
		cur_setting->it_value.tv_nsec -= NSEC_PER_SEC;
		cur_setting->it_value.tv_sec++;}
	cur_setting->it_interval.tv_nsec +=
		arch_cycle_to_nsec(timr->it_sub_incr);
	if ((cur_setting->it_interval.tv_nsec - NSEC_PER_SEC) >= 0) {
		cur_setting->it_interval.tv_nsec -= NSEC_PER_SEC;
		cur_setting->it_interval.tv_sec++;}
#endif
	if (cur_setting->it_value.tv_sec < 0) {
		cur_setting->it_value.tv_nsec = 1;
		cur_setting->it_value.tv_sec = 0;
	}
}

/* Get the time remaining on a POSIX.1b interval timer. */
asmlinkage long
sys_timer_gettime(timer_t timer_id, struct itimerspec __user *setting)
{
	struct k_itimer *timr;
	struct itimerspec cur_setting;
	unsigned long flags;

	timr = lock_timer(timer_id, &flags);
	if (!timr)
		return -EINVAL;

	p_timer_get(&posix_clocks[timr->it_clock], timr, &cur_setting);

	unlock_timer(timr, flags);

	if (copy_to_user(setting, &cur_setting, sizeof (cur_setting)))
		return -EFAULT;

	return 0;
}
/*
 * Get the number of overruns of a POSIX.1b interval timer.  This is to
 * be the overrun of the timer last delivered.  At the same time we are
 * accumulating overruns on the next timer.  The overrun is frozen when
 * the signal is delivered, either at the notify time (if the info block
 * is not queued) or at the actual delivery time (as we are informed by
 * the call back to do_schedule_next_timer().  So all we need to do is
 * to pick up the frozen overrun.
 */

asmlinkage long
sys_timer_getoverrun(timer_t timer_id)
{
	struct k_itimer *timr;
	int overrun;
	long flags;

	timr = lock_timer(timer_id, &flags);
	if (!timr)
		return -EINVAL;

	overrun = timr->it_overrun_last;
	unlock_timer(timr, flags);

	return overrun;
}
/*
 * Adjust for absolute time
 *
 * If absolute time is given and it is not CLOCK_MONOTONIC, we need to
 * adjust for the offset between the timer clock (CLOCK_MONOTONIC) and
 * what ever clock he is using.
 *
 * If it is relative time, we need to add the current (CLOCK_MONOTONIC)
 * time to it to get the proper time for the timer.
 *
 * err will be set to -EINVAL if offset is larger than MAX_JIFFY_OFFSET.
 * Caller should set err otherwise prior to call if he desires to test
 * this value.
 */
static int adjust_abs_time(struct k_clock *clock, struct timespec *tp, 
			   int abs, u64 *exp, int *err)
{
	struct timespec now;
	struct timespec oc = *tp;
	struct timespec wall_to_mono;
	u64 jiffies_64_f;
	long sub_jiff;
	int rtn =0;

	if (abs) {
		/*
		 * The mask picks up the 4 basic clocks 
		 */
		if ((clock - &posix_clocks[0]) <= CLOCK_MONOTONIC_HR) {
			jiffies_64_f = do_posix_clock_monotonic_gettime_parts(
				&now,  &wall_to_mono, &sub_jiff);
			/*
			 * If we are doing a MONOTONIC clock
			 */
			if(clock->clock_get ==
			    posix_clocks[CLOCK_MONOTONIC].clock_get){
				now.tv_sec += wall_to_mono.tv_sec;
				now.tv_nsec += wall_to_mono.tv_nsec;
			}
		} else {
			/*
			 * Not one of the basic clocks
			 */
			do_posix_gettime(clock, &now);	
			jiffies_64_f = get_jiffies_64();
		}
		/*
		 * Take away now to get delta
		 */
		oc.tv_sec -= now.tv_sec;
		oc.tv_nsec -= now.tv_nsec;
		/*
		 * Normalize...
		 */
		timespec_norm(&oc);
		while ((oc.tv_nsec) < 0) {
			oc.tv_nsec += NSEC_PER_SEC;
			oc.tv_sec--;
		}
	}else{
#ifdef CONFIG_HIGH_RES_TIMERS
		do_atomic_on_xtime_seq(
			jiffies_64_f = jiffies_64;
			sub_jiff = get_arch_cycles((u32) jiffies_64_f);
			);
#else
		jiffies_64_f = get_jiffies_64();
#endif
	}
	/*
	 * Check if the requested time is prior to now (if so set now)
	 */
	if (oc.tv_sec < 0)
		oc.tv_sec = oc.tv_nsec = 0;

	if (oc.tv_sec | oc.tv_nsec) {
		oc.tv_nsec += clock->res;
		timespec_norm(&oc);
	}

	IF_HIGH_RES(rtn = sub_jiff + )
		tstojiffie(&oc, clock->res, exp);

	/*
	 * return the actual jiffies expire time, full 64 bits
	 */
#ifdef CONFIG_HIGH_RES_TIMERS
	while (rtn >= arch_cycles_per_jiffy) {
		rtn -= arch_cycles_per_jiffy;
		jiffies_64_f++;
	}
#endif
	if (*exp > ((u64)MAX_JIFFY_OFFSET))
		*err = -EINVAL;

	*exp += jiffies_64_f;
	return rtn;
}

/* Set a POSIX.1b interval timer. */
/* timr->it_lock is taken. */
static inline int
do_timer_settime(struct k_itimer *timr, int flags,
		 struct itimerspec *new_setting, struct itimerspec *old_setting)
{
	struct k_clock *clock = &posix_clocks[timr->it_clock];
	u64 expire_64;
	int rtn = 0;
	IF_HIGH_RES(long sub_expire;)

	if (old_setting)
		do_timer_gettime(timr, old_setting);

	/* disable the timer */
	timr->it_incr = 0;
	IF_HIGH_RES(timr->it_sub_incr = 0;)
	/*
	 * careful here.  If smp we could be in the "fire" routine which will
	 * be spinning as we hold the lock.  But this is ONLY an SMP issue.
	 */
#ifdef CONFIG_SMP
	if (timer_active(timr) && !del_timer(&timr->it_timer))
		/*
		 * It can only be active if on an other cpu.  Since
		 * we have cleared the interval stuff above, it should
		 * clear once we release the spin lock.  Of course once
		 * we do that anything could happen, including the
		 * complete melt down of the timer.  So return with
		 * a "retry" exit status.
		 */
		return TIMER_RETRY;

	set_timer_inactive(timr);
#else
	del_timer(&timr->it_timer);
#endif
	timr->it_requeue_pending = (timr->it_requeue_pending + 2) & 
		~REQUEUE_PENDING;
	timr->it_overrun_last = 0;
	timr->it_overrun = -1;
	/*
	 *switch off the timer when it_value is zero
	 */
	if (!new_setting->it_value.tv_sec && !new_setting->it_value.tv_nsec) {
		timr->it_timer.expires = 0;
		IF_HIGH_RES(timr->it_timer.sub_expires = 0;)
		return 0;
	}

	IF_HIGH_RES(sub_expire = )
		adjust_abs_time(clock, &new_setting->it_value, 
				flags & TIMER_ABSTIME, &expire_64, &rtn);
	/*
	 * Check if the requested time is more than the timer code
	 * can handle (if so we error out).
	 */
	if (rtn)
			/*
			 * This is a considered response, not exactly in
			 * line with the standard (in fact it is silent on
			 * possible overflows).  We assume such a large 
			 * value is ALMOST always a programming error and
			 * try not to compound it by setting a really dumb
			 * value.
			 */
		return rtn;

	timr->it_timer.expires = (unsigned long)expire_64;	
	IF_HIGH_RES(timr->it_timer.sub_expires = sub_expire;)
	IF_HIGH_RES(timr->it_sub_incr =) 
		tstojiffie(&new_setting->it_interval, clock->res, &expire_64);

	timr->it_incr = (unsigned long)expire_64;


	/*
	 * For some reason the timer does not fire immediately if expires is
	 * equal to jiffies, so the timer notify function is called directly.
	 * We do not even queue SIGEV_NONE timers!
	 */
	if (!((timr->it_sigev_notify & ~SIGEV_THREAD_ID) == SIGEV_NONE)) {
#ifndef	 CONFIG_HIGH_RES_TIMERS
		if (timr->it_timer.expires == jiffies)
			timer_notify_task(timr);
		else
#endif
			add_timer(&timr->it_timer);
	}
	return 0;
}

/* Set a POSIX.1b interval timer */
asmlinkage long
sys_timer_settime(timer_t timer_id, int flags,
		  const struct itimerspec __user *new_setting,
		  struct itimerspec __user *old_setting)
{
	struct k_itimer *timr;
	struct itimerspec new_spec, old_spec;
	int error = 0;
	long flag;
	struct itimerspec *rtn = old_setting ? &old_spec : NULL;

	if (!new_setting)
		return -EINVAL;

	if (copy_from_user(&new_spec, new_setting, sizeof (new_spec)))
		return -EFAULT;

	if ((!good_timespec(&new_spec.it_interval)) ||
	    (!good_timespec(&new_spec.it_value)))
		return -EINVAL;
retry:
	timr = lock_timer(timer_id, &flag);
	if (!timr)
		return -EINVAL;

	if (!posix_clocks[timr->it_clock].timer_set)
		error = do_timer_settime(timr, flags, &new_spec, rtn);
	else
		error = posix_clocks[timr->it_clock].timer_set(timr,
							       flags,
							       &new_spec, rtn);
	unlock_timer(timr, flag);
	if (error == TIMER_RETRY) {
		rtn = NULL;	// We already got the old time...
		goto retry;
	}

	if (old_setting && !error && copy_to_user(old_setting,
						  &old_spec, sizeof (old_spec)))
		error = -EFAULT;

	return error;
}

static inline int do_timer_delete(struct k_itimer *timer)
{
	timer->it_incr = 0;
	IF_HIGH_RES(timer->it_sub_incr = 0;)
#ifdef CONFIG_SMP
	if (timer_active(timer) && !del_timer(&timer->it_timer))
		/*
		 * It can only be active if on an other cpu.  Since
		 * we have cleared the interval stuff above, it should
		 * clear once we release the spin lock.  Of course once
		 * we do that anything could happen, including the
		 * complete melt down of the timer.  So return with
		 * a "retry" exit status.
		 */
		return TIMER_RETRY;
#else
	del_timer(&timer->it_timer);
#endif
	return 0;
}

/* Delete a POSIX.1b interval timer. */
asmlinkage long
sys_timer_delete(timer_t timer_id)
{
	struct k_itimer *timer;
	long flags;

#ifdef CONFIG_SMP
	int error;
retry_delete:
#endif
	timer = lock_timer(timer_id, &flags);
	if (!timer)
		return -EINVAL;

#ifdef CONFIG_SMP
	error = p_timer_del(&posix_clocks[timer->it_clock], timer);

	if (error == TIMER_RETRY) {
		unlock_timer(timer, flags);
		goto retry_delete;
	}
#else
	p_timer_del(&posix_clocks[timer->it_clock], timer);
#endif
	task_lock(timer->it_process);
	list_del(&timer->list);
	task_unlock(timer->it_process);
	/*
	 * This keeps any tasks waiting on the spin lock from thinking
	 * they got something (see the lock code above).
	 */
	timer->it_process = NULL;
	unlock_timer(timer, flags);
	release_posix_timer(timer);
	return 0;
}
/*
 * return timer owned by the process, used by exit_itimers
 */
static inline void itimer_delete(struct k_itimer *timer)
{
	if (sys_timer_delete(timer->it_id))
		BUG();
}
/*
 * This is exported to exit and exec
 */
void exit_itimers(struct task_struct *tsk)
{
	struct k_itimer *tmr;

	task_lock(tsk);
	while (!list_empty(&tsk->posix_timers)) {
		tmr = list_entry(tsk->posix_timers.next, struct k_itimer, list);
		task_unlock(tsk);
		itimer_delete(tmr);
		task_lock(tsk);
	}
	task_unlock(tsk);
}

/*
 * And now for the "clock" calls
 *
 * These functions are called both from timer functions (with the timer
 * spin_lock_irq() held and from clock calls with no locking.	They must
 * use the save flags versions of locks.
 */
extern volatile unsigned long wall_jiffies;

static void get_wall_time(struct timespec *tp)
{
	struct timeval tv;

#ifdef CONFIG_HIGH_RES_TIMERS
	tv = xtime;
	tp->tv_nsec = arch_cycle_to_nsec(get_arch_cycles(wall_jiffies));
	tp->tv_nsec += tv.tv_usec * NSEC_PER_USEC;
	tp->tv_sec = tv.tv_sec;
#else
	do_gettimeofday(&tv);
	tp->tv_nsec = tv.tv_usec * NSEC_PER_USEC;
	tp->tv_sec = tv.tv_sec;
#endif
}
/*
 * The sequence lock below does not need the irq part with real
 * sequence locks (i.e. in 2.6).  We do need them in 2.4 where
 * we emulate the sequence lock... shame really.
 */
static int do_posix_gettime(struct k_clock *clock, struct timespec *tp)
{
	if (clock->clock_get)
		return clock->clock_get(tp);

	{

		unsigned int seq;
		unsigned long flags;
		do {
			seq = read_seqbegin_irqsave(&xtime_lock, flags);
			get_wall_time(tp);
		} while(read_seqretry_irqrestore(&xtime_lock, seq, flags));

		timespec_norm(tp);
	}
	return 0;
}

/*
 * The sequence lock below does not need the irq part in 2.6.  In 2.4
 * we need it as we really use a read lock.
 */

static u64 do_posix_clock_monotonic_gettime_parts(
	struct timespec *tp, struct timespec *mo, long *sub_jiff)
{
	u64 jiff;

	unsigned int seq;
	unsigned long flags;
	do {
		seq = read_seqbegin_irqsave(&xtime_lock, flags);
		get_wall_time(tp);
		*mo = wall_to_monotonic;
		jiff = jiffies_64;
		IF_HIGH_RES(*sub_jiff = get_arch_cycles((u32)jiff);)
	} while(read_seqretry_irqrestore(&xtime_lock, seq, flags));
	return jiff;
}

int do_posix_clock_monotonic_gettime(struct timespec *tp)
{
	struct timespec wall_to_mono;
	long sub_jiff;

	do_posix_clock_monotonic_gettime_parts(tp, &wall_to_mono, &sub_jiff);

	tp->tv_sec += wall_to_mono.tv_sec;
	tp->tv_nsec += wall_to_mono.tv_nsec;

	timespec_norm(tp);
	return 0;
}

int do_posix_clock_monotonic_settime(struct timespec *tp)
{
	return -EINVAL;
}

asmlinkage long
sys_clock_settime(clockid_t which_clock, const struct timespec __user *tp)
{
	struct timespec new_tp;

	if ((unsigned) which_clock >= MAX_CLOCKS ||
					!posix_clocks[which_clock].res)
		return -EINVAL;
	if (copy_from_user(&new_tp, tp, sizeof (*tp)))
		return -EFAULT;
	if (posix_clocks[which_clock].clock_set)
		return posix_clocks[which_clock].clock_set(&new_tp);

	new_tp.tv_nsec /= NSEC_PER_USEC;
	return do_sys_settimeofday((struct timeval *) &new_tp, NULL);
}

asmlinkage long
sys_clock_gettime(clockid_t which_clock, struct timespec __user *tp)
{
	struct timespec rtn_tp;
	int error = 0;

	if ((unsigned) which_clock >= MAX_CLOCKS ||
					!posix_clocks[which_clock].res)
		return -EINVAL;

	error = do_posix_gettime(&posix_clocks[which_clock], &rtn_tp);

	if (!error && copy_to_user(tp, &rtn_tp, sizeof (rtn_tp)))
		error = -EFAULT;

	return error;

}

asmlinkage long
sys_clock_getres(clockid_t which_clock, struct timespec __user *tp)
{
	struct timespec rtn_tp;

	if ((unsigned) which_clock >= MAX_CLOCKS ||
					!posix_clocks[which_clock].res)
		return -EINVAL;

	rtn_tp.tv_sec = 0;
	rtn_tp.tv_nsec = posix_clocks[which_clock].res;
	if (tp && copy_to_user(tp, &rtn_tp, sizeof (rtn_tp)))
		return -EFAULT;

	return 0;

}

static void nanosleep_wake_up(unsigned long __data)
{
	struct task_struct *p = (struct task_struct *) __data;

	wake_up_process(p);
}

/*
 * The standard says that an absolute nanosleep call MUST wake up at
 * the requested time in spite of clock settings.  Here is what we do:
 * For each nanosleep call that needs it (only absolute and not on
 * CLOCK_MONOTONIC* (as it can not be set)) we thread a little structure
 * into the "nanosleep_abs_list".  All we need is the task_struct pointer.
 * When ever the clock is set we just wake up all those tasks.	 The rest
 * is done by the while loop in clock_nanosleep().
 *
 * On locking, clock_was_set() is called from update_wall_clock which
 * holds (or has held for it) a write_lock_irq( xtime_lock) and is
 * called from the timer bh code.  Thus we need the irq save locks.
 */

static DECLARE_WAIT_QUEUE_HEAD(nanosleep_abs_wqueue);

void clock_was_set(void)
{
	wake_up_all(&nanosleep_abs_wqueue);
}

long clock_nanosleep_restart(struct restart_block *restart_block);

extern long do_clock_nanosleep(clockid_t which_clock, int flags,
			       struct timespec *t);

#ifdef FOLD_NANO_SLEEP_INTO_CLOCK_NANO_SLEEP

asmlinkage long
sys_nanosleep(struct timespec __user *rqtp, struct timespec __user *rmtp)
{
	struct timespec t;
	long ret;

	if (copy_from_user(&t, rqtp, sizeof (t)))
		return -EFAULT;

	if ((unsigned) t.tv_nsec >= NSEC_PER_SEC || t.tv_sec < 0)
		return -EINVAL;

	ret = do_clock_nanosleep(CLOCK_REALTIME, 0, &t);

	if (ret == -ERESTART_RESTARTBLOCK && rmtp &&
					copy_to_user(rmtp, &t, sizeof (t)))
		return -EFAULT;
	return ret;
}
#endif				// ! FOLD_NANO_SLEEP_INTO_CLOCK_NANO_SLEEP

asmlinkage long
sys_clock_nanosleep(clockid_t which_clock, int flags,
		    const struct timespec __user *rqtp,
		    struct timespec __user *rmtp)
{
	struct timespec t;
	int ret;

	if ((unsigned) which_clock >= MAX_CLOCKS ||
					!posix_clocks[which_clock].res)
		return -EINVAL;

	if (copy_from_user(&t, rqtp, sizeof (struct timespec)))
		return -EFAULT;

	if ((unsigned) t.tv_nsec >= NSEC_PER_SEC || t.tv_sec < 0)
		return -EINVAL;

	ret = do_clock_nanosleep(which_clock, flags, &t);

	if ((ret == -ERESTART_RESTARTBLOCK) && rmtp &&
					copy_to_user(rmtp, &t, sizeof (t)))
		return -EFAULT;
	return ret;
}
#ifdef CONFIG_HIGH_RES_TIMERS
#define get_jiffies_time(result) 				\
({								\
	unsigned int seq; 					\
	u64 jiff;						\
	do {							\
		seq = read_seqbegin(&xtime_lock);		\
		jiff = jiffies_64;				\
		*result = get_arch_cycles((unsigned long)jiff);	\
	} while(read_seqretry(&xtime_lock, seq));		\
	jiff;							\
})

#else
#define get_jiffies_time(result) get_jiffies_64()

#endif

long
do_clock_nanosleep(clockid_t which_clock, int flags, struct timespec *tsave)
{
        struct timespec t;
	struct timer_list new_timer;
	DECLARE_WAITQUEUE(abs_wqueue, current);
	u64 rq_time = (u64)0;
	s64 left;
	IF_HIGH_RES(long sub_left;)
	int abs;
	struct restart_block *restart_block =
	    &(current_thread_info()->restart_block);

	abs_wqueue.flags = 0;
	init_timer(&new_timer);
	new_timer.expires = 0;
	new_timer.data = (unsigned long) current;
	new_timer.function = nanosleep_wake_up;
	abs = flags & TIMER_ABSTIME;

	if (restart_block->fn == clock_nanosleep_restart) {
		/*
		 * Interrupted by a non-delivered signal, pick up remaining
		 * time and continue.
		 */
		restart_block->fn = do_no_restart_syscall;

		rq_time = restart_block->arg3;
		rq_time = (rq_time << 32) + restart_block->arg2;
		if (!rq_time)
			return -EINTR;
		IF_HIGH_RES(new_timer.sub_expires = restart_block->arg4;)
		left = rq_time - get_jiffies_time(&sub_left);


#ifndef CONFIG_HIGH_RES_TIMERS
		if (left <= (s64)0)
			return 0;	/* Already passed */
#else
		if ((left < (s64)0) || 
		    ((left == (s64)0) && (new_timer.sub_expires <= sub_left)))
			return 0;
#endif
	}

	if (abs && (posix_clocks[which_clock].clock_get !=
			    posix_clocks[CLOCK_MONOTONIC].clock_get))
		add_wait_queue(&nanosleep_abs_wqueue, &abs_wqueue);

	do {
		t = *tsave;
		if (abs || !(rq_time IF_HIGH_RES(| new_timer.sub_expires))) {
			int dum;
			IF_HIGH_RES(new_timer.sub_expires =)
			adjust_abs_time(&posix_clocks[which_clock], &t, abs,
					&rq_time, &dum);
		}

		left = rq_time - get_jiffies_time(&sub_left);
		if (left >= (s64)MAX_JIFFY_OFFSET)
			left = (s64)MAX_JIFFY_OFFSET;
#ifndef CONFIG_HIGH_RES_TIMERS
		if (left < (s64)0)
			break;
#else
		if ((left < (s64)0) || 
		    ((left == (s64)0) && (new_timer.sub_expires <= sub_left)))
			break;
#endif

		new_timer.expires = jiffies + left;
		__set_current_state(TASK_INTERRUPTIBLE);
		add_timer(&new_timer);

		schedule();

		del_timer_sync(&new_timer);
		left = rq_time - get_jiffies_time(&sub_left);
	} while ((left > (s64)0  
#ifdef CONFIG_HIGH_RES_TIMERS
		  || (left == (s64)0 && (new_timer.sub_expires > sub_left))
#endif
			 ) &&  !test_thread_flag(TIF_SIGPENDING));

	if (abs_wqueue.task_list.next)
		finish_wait(&nanosleep_abs_wqueue, &abs_wqueue);


#ifdef CONFIG_HIGH_RES_TIMERS
	if (left > (s64)0 || 
	    (left == (s64)0 && (new_timer.sub_expires > sub_left)))
#else
	if (left > (s64)0 )
#endif
	{
		/*
		 * Always restart abs calls from scratch to pick up any
		 * clock shifting that happened while we are away.
		 */
		if (abs)
			return -ERESTARTNOHAND;

		left *= TICK_NSEC;
#ifdef CONFIG_HIGH_RES_TIMERS
		left += arch_cycle_to_nsec(new_timer.sub_expires - sub_left);
#endif
		tsave->tv_sec = div_long_long_rem(left, 
						  NSEC_PER_SEC, 
						  &tsave->tv_nsec);

		restart_block->fn = clock_nanosleep_restart;
		restart_block->arg0 = which_clock;
		restart_block->arg1 = (unsigned long)tsave;
		restart_block->arg2 = rq_time & 0xffffffffLL;
		restart_block->arg3 = rq_time >> 32;
		IF_HIGH_RES(restart_block->arg4 = new_timer.sub_expires;)

		return -ERESTART_RESTARTBLOCK;
	}

	return 0;
}
/*
 * This will restart either clock_nanosleep or clock_nanosleep
 */
long
clock_nanosleep_restart(struct restart_block *restart_block)
{
	struct timespec t;
	int ret = do_clock_nanosleep(restart_block->arg0, 0, &t);

	if ((ret == -ERESTART_RESTARTBLOCK) && restart_block->arg1 &&
	    copy_to_user((struct timespec __user *)(restart_block->arg1), &t,
			 sizeof (t)))
		return -EFAULT;
	return ret;
}
