#include <nucleus/timebase.h>
#include <nucleus/timer.h>
#include <nucleus/shadow.h>
#include <nucleus/thread.h>
#include <nucleus/heap.h>
#include <nucleus/pod.h>
#include <nucleus/types.h>
#include <testing/sigtest_syscall.h>

static int muxid;
static xntbase_t *tbase;

static int *sigs, next_sig;
static size_t nr_sigs;
static xnthread_t *target;
static xntimer_t sigtest_timer;

MODULE_DESCRIPTION("signals testing interface");
MODULE_AUTHOR("gilles.chanteperdrix@xenomai.org");
MODULE_LICENSE("GPL");

static void sigtest_timer_handler(xntimer_t *timer)
{
	xnshadow_mark_sig(target, muxid);
	/* xnpod_schedule called later. */
}

static int __sigtest_queue(struct pt_regs *regs)
{
	target = xnshadow_thread(current);
	if (!target)
		return -EPERM;

	nr_sigs = (size_t)__xn_reg_arg2(regs);
	sigs = xnmalloc(sizeof(*sigs) * nr_sigs);
	next_sig = 0;

	if (__xn_copy_from_user(sigs, (void __user *)__xn_reg_arg1(regs),
				sizeof(*sigs) * nr_sigs)) {
		xnfree(sigs);
		return -EFAULT;
	}

	xntimer_set_sched(&sigtest_timer, xnpod_current_sched());
	xntimer_start(&sigtest_timer, 10000000, 0, 0);

	return 0;
}

static int __sigtest_wait_pri(struct pt_regs *regs)
{
	xnthread_t *thread = xnshadow_thread(current);
	xnticks_t ticks = xntbase_ns2ticks(tbase, 20000000);
	xnpod_suspend_thread(thread, XNDELAY, ticks, XN_RELATIVE, NULL);
	if (xnthread_test_info(thread, XNBREAK))
		return -EINTR;

	return 0;
}

static int __sigtest_wait_sec(struct pt_regs *regs)
{
	schedule_timeout_interruptible(20 * HZ / 1000 + 1);
	if (signal_pending(current))
		return -EINTR;
	return 0;
}

static xnsysent_t __systab[] = {
	[__NR_sigtest_queue] = {&__sigtest_queue, __xn_exec_any},
	[__NR_sigtest_wait_pri] = {&__sigtest_wait_pri, __xn_exec_primary},
	[__NR_sigtest_wait_sec] = {&__sigtest_wait_sec, __xn_exec_secondary},
};

static int sigtest_unqueue(xnthread_t *thread, union xnsiginfo __user *si)
{
	struct sigtest_siginfo __user *mysi = (struct sigtest_siginfo __user *)si;
	int status = sigs[next_sig];

	__xn_put_user(next_sig, &mysi->sig_nr);
	if (++next_sig == nr_sigs) {
		spl_t s;

		xnfree(sigs);
		xnlock_get_irqsave(&nklock, s);
		xnshadow_clear_sig(thread, muxid);
		xnlock_put_irqrestore(&nklock, s);
	}
	return status;
}

static struct xnskin_props __props = {
	.name = "sigtest",
	.magic = SIGTEST_SKIN_MAGIC,
	.nrcalls = ARRAY_SIZE(__systab),
	.systab = __systab,
	.eventcb = NULL,
	.sig_unqueue = sigtest_unqueue,
	.timebasep = &tbase,
	.module = THIS_MODULE
};

int SKIN_INIT(sigtest)
{
	int err;

	xnprintf("starting sigtest services\n");

	err = xnpod_init();
	if (err)
		goto fail;

	err = xntbase_alloc("sigtest", 0, 0, &tbase);
	if (err)
		goto fail_shutdown_pod;

	muxid = xnshadow_register_interface(&__props);
	if (muxid < 0) {
		err = muxid;
	  fail_shutdown_pod:
		xnpod_shutdown(err);
	  fail:
		return err;
	}

	xntimer_init(&sigtest_timer, tbase, sigtest_timer_handler);

	return 0;
}

void SKIN_EXIT(sigtest)
{
	xnprintf("stopping sigtest services\n");
	xntimer_destroy(&sigtest_timer);
	xnshadow_unregister_interface(muxid);
	xntbase_free(tbase);
	xnpod_shutdown(XNPOD_NORMAL_EXIT);
}
module_init(__sigtest_skin_init);
module_exit(__sigtest_skin_exit);
