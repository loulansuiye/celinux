/*
 * <asm/smplock.h>
 *
 * Default SMP lock implementation
 */
#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <asm/current.h>

extern spinlock_t kernel_flag;

#ifdef CONFIG_PREEMPT
#define kernel_locked()		preempt_get_count()
#else
#define kernel_locked()		spin_is_locked(&kernel_flag)
#endif

/*
 * Release global kernel lock and global interrupt lock
 */
#define release_kernel_lock(task, cpu) \
do { \
	if (task->lock_depth >= 0) \
		spin_unlock(&kernel_flag); \
	release_irqlock(cpu); \
	__sti(); \
} while (0)

/*
 * Re-acquire the kernel lock
 */
#define reacquire_kernel_lock(task) \
do { \
	if (task->lock_depth >= 0) \
		spin_lock(&kernel_flag); \
} while (0)


/*
 * Getting the big kernel lock.
 *
 * This cannot happen asynchronously,
 * so we only need to worry about other
 * CPU's.
 */
static inline void lock_kernel(void)
{
#ifdef CONFIG_PREEMPT
	if (current->lock_depth == -1)
		spin_lock(&kernel_flag);
	++current->lock_depth;
#else
	if (!++current->lock_depth)
		spin_lock(&kernel_flag);
#endif
}

static inline void unlock_kernel(void)
{
	if (--current->lock_depth < 0)
		spin_unlock(&kernel_flag);
}
