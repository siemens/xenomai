/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

/*!
 * \defgroup nucleus Xenomai nucleus.
 *
 * An abstract RTOS core.
 */

#include <nucleus/module.h>
#include <nucleus/pod.h>
#include <nucleus/timer.h>
#include <nucleus/heap.h>
#include <nucleus/version.h>
#ifdef CONFIG_XENO_OPT_PIPE
#include <nucleus/pipe.h>
#endif /* CONFIG_XENO_OPT_PIPE */
#ifdef CONFIG_XENO_OPT_PERVASIVE
#include <nucleus/core.h>
#endif /* CONFIG_XENO_OPT_PERVASIVE */
#include <nucleus/ltt.h>
#include <asm/xenomai/bits/init.h>

MODULE_DESCRIPTION("Xenomai nucleus");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

u_long sysheap_size_arg = XNPOD_HEAPSIZE / 1024;
module_param_named(sysheap_size, sysheap_size_arg, ulong, 0444);
MODULE_PARM_DESC(sysheap_size, "System heap size (Kb)");

xnqueue_t xnmod_glink_queue;

u_long xnmod_sysheap_size;

int xeno_nucleus_status = -EINVAL;

void xnmod_alloc_glinks(xnqueue_t *freehq)
{
	xngholder_t *sholder, *eholder;

	sholder =
	    (xngholder_t *) xnheap_alloc(&kheap,
					 sizeof(xngholder_t) *
					 XNMOD_GHOLDER_REALLOC);

	if (!sholder) {
		/* If we are running out of memory but still have some free
		   holders, just return silently, hoping that the contention
		   will disappear before we have no other choice than
		   allocating memory eventually. Otherwise, we have to raise a
		   fatal error right now. */

		if (emptyq_p(freehq))
			xnpod_fatal("cannot allocate generic holders");

		return;
	}

	for (eholder = sholder + XNMOD_GHOLDER_REALLOC;
	     sholder < eholder; sholder++) {
		inith(&sholder->glink.plink);
		appendq(freehq, &sholder->glink.plink);
	}
}

#if defined(CONFIG_PROC_FS) && defined(__KERNEL__)

#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>

extern struct proc_dir_entry *rthal_proc_root;

#ifdef CONFIG_XENO_OPT_PERVASIVE
static struct proc_dir_entry *iface_proc_root;
#endif /* CONFIG_XENO_OPT_PERVASIVE */

struct sched_seq_iterator {
	xnticks_t start_time;
	int nentries;
	struct sched_seq_info {
		int cpu;
		pid_t pid;
		char name[XNOBJECT_NAME_LEN];
		int cprio;
		xnticks_t period;
		xnticks_t timeout;
		xnflags_t status;
	} sched_info[1];
};

static void *sched_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct sched_seq_iterator *iter =
	    (struct sched_seq_iterator *)seq->private;

	if (*pos > iter->nentries)
		return NULL;

	if (*pos == 0)
		return SEQ_START_TOKEN;

	return iter->sched_info + *pos - 1;
}

static void *sched_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sched_seq_iterator *iter =
	    (struct sched_seq_iterator *)seq->private;

	++*pos;

	if (v == SEQ_START_TOKEN)
		return &iter->sched_info[0];

	if (*pos > iter->nentries)
		return NULL;

	return iter->sched_info + *pos - 1;
}

static void sched_seq_stop(struct seq_file *seq, void *v)
{
}

static int sched_seq_show(struct seq_file *seq, void *v)
{
	char sbuf[64], pbuf[16];

	if (v == SEQ_START_TOKEN)
		seq_printf(seq, "%-3s  %-6s %-8s %-8s %-10s %-10s %s\n",
			   "CPU", "PID", "PRI", "PERIOD", "TIMEOUT", "STAT", "NAME");
	else {
		struct sched_seq_info *p = (struct sched_seq_info *)v;

		if (p->status & XNINVPS)
			snprintf(pbuf, sizeof(pbuf), "%3d(%d)",
				 p->cprio, xnpod_rescale_prio(p->cprio));
		else
			snprintf(pbuf, sizeof(pbuf), "%3d", p->cprio);

		seq_printf(seq, "%3u  %-6d %-8s %-8Lu %-10Lu %-10s %s\n",
			   p->cpu,
			   p->pid,
			   pbuf,
			   p->period,
			   p->timeout,
			   xnthread_symbolic_status(p->status, sbuf,
						    sizeof(sbuf)), p->name);
	}

	return 0;
}

static struct seq_operations sched_op = {
	.start = &sched_seq_start,
	.next = &sched_seq_next,
	.stop = &sched_seq_stop,
	.show = &sched_seq_show
};

static int sched_seq_open(struct inode *inode, struct file *file)
{
	struct sched_seq_iterator *iter = NULL;
	struct seq_file *seq;
	xnholder_t *holder;
	int err, count, rev;
	spl_t s;

	if (!nkpod)
		return -ESRCH;

      restart:
	xnlock_get_irqsave(&nklock, s);

	rev = nkpod->threadq_rev;
	count = countq(&nkpod->threadq);	/* Cannot be empty (ROOT) */
	holder = getheadq(&nkpod->threadq);

	xnlock_put_irqrestore(&nklock, s);

	if (iter)
		kfree(iter);
	iter = kmalloc(sizeof(*iter)
		       + (count - 1) * sizeof(struct sched_seq_info),
		       GFP_KERNEL);
	if (!iter)
		return -ENOMEM;

	err = seq_open(file, &sched_op);

	if (err) {
		kfree(iter);
		return err;
	}

	iter->nentries = 0;
	iter->start_time = xntimer_get_jiffies();

	/* Take a snapshot element-wise, restart if something changes
	   underneath us. */

	while (holder) {
		xnthread_t *thread;
		int n;

		xnlock_get_irqsave(&nklock, s);

		if (nkpod->threadq_rev != rev)
			goto restart;
		rev = nkpod->threadq_rev;

		thread = link2thread(holder, glink);
		n = iter->nentries++;

		iter->sched_info[n].cpu = xnsched_cpu(thread->sched);
		iter->sched_info[n].pid = xnthread_user_pid(thread);
		memcpy(iter->sched_info[n].name, thread->name, sizeof(iter->sched_info[n].name));
		iter->sched_info[n].cprio = thread->cprio;
		iter->sched_info[n].period = xnthread_get_period(thread);
		iter->sched_info[n].timeout = xnthread_get_timeout(thread, iter->start_time);
		iter->sched_info[n].status = thread->status;

		holder = nextq(&nkpod->threadq, holder);

		xnlock_put_irqrestore(&nklock, s);
	}

	seq = (struct seq_file *)file->private_data;
	seq->private = iter;

	return 0;
}

static struct file_operations sched_seq_operations = {
	.owner = THIS_MODULE,
	.open = sched_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

#ifdef CONFIG_XENO_OPT_STATS

struct stat_seq_iterator {
	int nentries;
	struct stat_seq_info {
		int cpu;
		pid_t pid;
		xnflags_t status;
		char name[XNOBJECT_NAME_LEN];
		unsigned long ssw;
		unsigned long csw;
		unsigned long pf;
		xnticks_t runtime;
		xnticks_t account_period;
	} stat_info[1];
};

static void *stat_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct stat_seq_iterator *iter =
	    (struct stat_seq_iterator *)seq->private;

	if (*pos > iter->nentries)
		return NULL;

	if (*pos == 0)
		return SEQ_START_TOKEN;

	return iter->stat_info + *pos - 1;
}

static void *stat_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct stat_seq_iterator *iter =
	    (struct stat_seq_iterator *)seq->private;

	++*pos;

	if (v == SEQ_START_TOKEN)
		return &iter->stat_info[0];

	if (*pos > iter->nentries)
		return NULL;

	return iter->stat_info + *pos - 1;
}

static void stat_seq_stop(struct seq_file *seq, void *v)
{
}

static int stat_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_printf(seq, "%-3s  %-6s %-10s %-10s %-4s  %-8s  %5s"
			   "  %s\n",
			   "CPU", "PID", "MSW", "CSW", "PF", "STAT", "%CPU",
			   "NAME");
	else {
		struct stat_seq_info *p = (struct stat_seq_info *)v;
		int usage = 0;

		if (p->account_period) {
			while (p->account_period > 0xFFFFFFFF) {
				p->runtime >>= 16;
				p->account_period >>= 16;
			}
			usage =
			    xnarch_ulldiv(p->runtime * 1000LL +
					  (p->account_period >> 1),
					  p->account_period, NULL);
		}
		seq_printf(seq, "%3u  %-6d %-10lu %-10lu %-4lu  %.8lx  %3u.%u"
			   "  %s\n",
			   p->cpu, p->pid, p->ssw, p->csw, p->pf, p->status,
			   usage / 10, usage % 10, p->name);
	}

	return 0;
}

static struct seq_operations stat_op = {
	.start = &stat_seq_start,
	.next = &stat_seq_next,
	.stop = &stat_seq_stop,
	.show = &stat_seq_show
};

static int stat_seq_open(struct inode *inode, struct file *file)
{
	struct stat_seq_iterator *iter = NULL;
	struct seq_file *seq;
	xnholder_t *holder;
	struct stat_seq_info *stat_info;
	int err, count, thrq_rev, intr_rev, irq;
	spl_t s;

	if (!nkpod)
		return -ESRCH;

      restart:
	xnlock_get_irqsave(&nklock, s);

	count = countq(&nkpod->threadq);	/* Cannot be empty (ROOT) */
	holder = getheadq(&nkpod->threadq);
	thrq_rev = nkpod->threadq_rev;

	count += xnintr_count * RTHAL_NR_CPUS;
	intr_rev = xnintr_list_rev;

	xnlock_put_irqrestore(&nklock, s);

	if (iter)
		kfree(iter);
	iter = kmalloc(sizeof(*iter)
		       + (count - 1) * sizeof(struct stat_seq_info),
		       GFP_KERNEL);
	if (!iter)
		return -ENOMEM;

	err = seq_open(file, &stat_op);

	if (err) {
		kfree(iter);
		return err;
	}

	iter->nentries = 0;

	/* Take a snapshot element-wise, restart if something changes
	   underneath us. */

	while (holder) {
		xnthread_t *thread;
		xnsched_t *sched;
		xnticks_t period;

		xnlock_get_irqsave(&nklock, s);

		if (nkpod->threadq_rev != thrq_rev)
			goto restart;

		thread = link2thread(holder, glink);
		stat_info = &iter->stat_info[iter->nentries++];

		sched = thread->sched;
		stat_info->cpu = xnsched_cpu(sched);
		stat_info->pid = xnthread_user_pid(thread);
		memcpy(stat_info->name, thread->name,
		       sizeof(stat_info->name));
		stat_info->status = thread->status;
		stat_info->ssw = xnstat_counter_get(&thread->stat.ssw);
		stat_info->csw = xnstat_counter_get(&thread->stat.csw);
		stat_info->pf = xnstat_counter_get(&thread->stat.pf);

		period = sched->last_account_switch - thread->stat.account.start;
		if (!period && thread == sched->runthread) {
			stat_info->runtime = 1;
			stat_info->account_period = 1;
		} else {
			stat_info->runtime = thread->stat.account.total;
			stat_info->account_period = period;
		}
		thread->stat.account.total = 0;
		thread->stat.account.start = sched->last_account_switch;

		holder = nextq(&nkpod->threadq, holder);

		xnlock_put_irqrestore(&nklock, s);
	}

	/* Iterate over all IRQ numbers, ... */
	for (irq = 0; irq < RTHAL_NR_IRQS; irq++) {
		xnintr_t *prev = NULL;
		int cpu = 0;
		int err;

		/* ...over all shared IRQs on all CPUs */
		while (1) {
			stat_info = &iter->stat_info[iter->nentries];
			stat_info->cpu = cpu;

			err = xnintr_query(irq, &cpu, &prev, intr_rev,
					   stat_info->name,
					   &stat_info->csw,
					   &stat_info->runtime,
					   &stat_info->account_period);
			if (err == EAGAIN)
				goto restart;
			if (err)
				break; /* line unused or end of chain */

			stat_info->pid = 0;
			stat_info->status =  0;
			stat_info->ssw = 0;
			stat_info->pf = 0;

			iter->nentries++;
		};
	}

	seq = (struct seq_file *)file->private_data;
	seq->private = iter;

	return 0;
}

static struct file_operations stat_seq_operations = {
	.owner = THIS_MODULE,
	.open = stat_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

#endif /* CONFIG_XENO_OPT_STATS */

#if defined(CONFIG_SMP) && XENO_DEBUG(NUCLEUS)

xnlockinfo_t xnlock_stats[RTHAL_NR_CPUS];

static int lock_read_proc(char *page,
			  char **start,
			  off_t off, int count, int *eof, void *data)
{
	xnlockinfo_t lockinfo;
	int cpu, len = 0;
	char *p = page;
	spl_t s;

	for_each_online_cpu(cpu) {

		xnlock_get_irqsave(&nklock, s);
		lockinfo = xnlock_stats[cpu];
		xnlock_put_irqrestore(&nklock, s);

		if (cpu > 0)
			p += sprintf(p, "\n");

		p += sprintf(p, "CPU%d:\n", cpu);

		p += sprintf(p,
			     "  longest locked section: %llu ns\n"
			     "  spinning time: %llu ns\n"
			     "  section entry: %s:%d (%s)\n",
			     xnarch_tsc_to_ns(lockinfo.lock_time),
			     xnarch_tsc_to_ns(lockinfo.spin_time),
			     lockinfo.file, lockinfo.line, lockinfo.function);
	}

	len = p - page - off;

	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;

	return len;
}

EXPORT_SYMBOL(xnlock_stats);

#endif /* CONFIG_SMP && XENO_DEBUG(NUCLEUS) */

static int latency_read_proc(char *page,
			     char **start,
			     off_t off, int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%Lu\n", xnarch_tsc_to_ns(nkschedlat));
	len -= off;
	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;

	return len;
}

static int latency_write_proc(struct file *file,
			      const char __user * buffer,
			      unsigned long count, void *data)
{
	char *end, buf[16];
	long ns;
	int n;

	n = count > sizeof(buf) - 1 ? sizeof(buf) - 1 : count;

	if (copy_from_user(buf, buffer, n))
		return -EFAULT;

	buf[n] = '\0';
	ns = simple_strtol(buf, &end, 0);

	if ((*end != '\0' && !isspace(*end)) || ns < 0)
		return -EINVAL;

	nkschedlat = xnarch_ns_to_tsc(ns);

	return count;
}

static int version_read_proc(char *page,
			     char **start,
			     off_t off, int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%s\n", XENO_VERSION_STRING);
	len -= off;
	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;

	return len;
}

static int timer_read_proc(char *page,
			   char **start,
			   off_t off, int count, int *eof, void *data)
{
	const char *tm_status = "off", *wd_status = "";
	xnticks_t jiffies = 0, tickval = 0;
	int len;

	if (nkpod && testbits(nkpod->status, XNTIMED)) {
		tm_status = nktimer->get_type();
		tickval = xnpod_get_tickval();
		jiffies = xntimer_get_jiffies();
#ifdef CONFIG_XENO_OPT_WATCHDOG
		wd_status = "+watchdog";
#endif /* CONFIG_XENO_OPT_WATCHDOG */
	}

	len = sprintf(page,
		      "status=%s%s:setup=%Lu:tickval=%Lu:jiffies=%Lu\n",
		      tm_status, wd_status, xnarch_tsc_to_ns(nktimerlat), tickval, jiffies);

	len -= off;
	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;

	return len;
}

static int irq_read_proc(char *page,
			 char **start,
			 off_t off, int count, int *eof, void *data)
{
	int len = 0, cpu, irq;
	char *p = page;

	p += sprintf(p, "IRQ ");

	for_each_online_cpu(cpu) {
		p += sprintf(p, "        CPU%d", cpu);
	}

	for (irq = 0; irq < RTHAL_NR_IRQS; irq++) {

		if (rthal_irq_handler(&rthal_domain, irq) == NULL)
			continue;

		p += sprintf(p, "\n%3d:", irq);

		for_each_online_cpu(cpu) {
			p += sprintf(p, "%12lu",
				     rthal_cpudata_irq_hits(&rthal_domain, cpu,
							    irq));
		}

		p += xnintr_irq_proc(irq, p);
	}

	p += sprintf(p, "\n");

	len = p - page - off;
	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;

	return len;
}

static int heap_read_proc(char *page,
			  char **start,
			  off_t off, int count, int *eof, void *data)
{
	int len;

	if (!nkpod)
		return -ESRCH;

	len = sprintf(page, "size=%lu:used=%lu:pagesz=%lu\n",
		      xnheap_usable_mem(&kheap),
		      xnheap_used_mem(&kheap),
		      xnheap_page_size(&kheap));

	len -= off;
	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;

	return len;
}

static struct proc_dir_entry *add_proc_leaf(const char *name,
					    read_proc_t rdproc,
					    write_proc_t wrproc,
					    void *data,
					    struct proc_dir_entry *parent)
{
	int mode = wrproc ? 0644 : 0444;
	struct proc_dir_entry *entry;

	entry = create_proc_entry(name, mode, parent);

	if (!entry)
		return NULL;

	entry->nlink = 1;
	entry->data = data;
	entry->read_proc = rdproc;
	entry->write_proc = wrproc;
	entry->owner = THIS_MODULE;

	return entry;
}

static struct proc_dir_entry *add_proc_fops(const char *name,
					    struct file_operations *fops,
					    size_t size,
					    struct proc_dir_entry *parent)
{
	struct proc_dir_entry *entry;

	entry = create_proc_entry(name, 0, parent);

	if (!entry)
		return NULL;

	entry->proc_fops = fops;
	entry->owner = THIS_MODULE;

	if (size)
		entry->size = size;

	return entry;
}

void xnpod_init_proc(void)
{
	if (!rthal_proc_root)
		return;

	add_proc_fops("sched", &sched_seq_operations, 0, rthal_proc_root);

#ifdef CONFIG_XENO_OPT_STATS
	add_proc_fops("stat", &stat_seq_operations, 0, rthal_proc_root);
#endif /* CONFIG_XENO_OPT_STATS */

#if defined(CONFIG_SMP) && XENO_DEBUG(NUCLEUS)
	add_proc_leaf("lock", &lock_read_proc, NULL, NULL, rthal_proc_root);
#endif /* CONFIG_SMP && XENO_DEBUG(NUCLEUS) */

	add_proc_leaf("latency",
		      &latency_read_proc,
		      &latency_write_proc, NULL, rthal_proc_root);

	add_proc_leaf("version", &version_read_proc, NULL, NULL,
		      rthal_proc_root);

	add_proc_leaf("timer", &timer_read_proc, NULL, NULL, rthal_proc_root);

	add_proc_leaf("irq", &irq_read_proc, NULL, NULL, rthal_proc_root);

	add_proc_leaf("heap", &heap_read_proc, NULL, NULL, rthal_proc_root);

#ifdef CONFIG_XENO_OPT_PERVASIVE
	iface_proc_root =
	    create_proc_entry("interfaces", S_IFDIR, rthal_proc_root);
#endif /* CONFIG_XENO_OPT_PERVASIVE */
}

void xnpod_delete_proc(void)
{
#ifdef CONFIG_XENO_OPT_PERVASIVE
	int muxid;

	for (muxid = 0; muxid < XENOMAI_MUX_NR; muxid++)
		if (muxtable[muxid].proc)
			remove_proc_entry(muxtable[muxid].name,
					  iface_proc_root);

	remove_proc_entry("interfaces", rthal_proc_root);
#endif /* CONFIG_XENO_OPT_PERVASIVE */
	remove_proc_entry("heap", rthal_proc_root);
	remove_proc_entry("irq", rthal_proc_root);
	remove_proc_entry("timer", rthal_proc_root);
	remove_proc_entry("version", rthal_proc_root);
	remove_proc_entry("latency", rthal_proc_root);
	remove_proc_entry("sched", rthal_proc_root);
#ifdef CONFIG_XENO_OPT_STATS
	remove_proc_entry("stat", rthal_proc_root);
#endif /* CONFIG_XENO_OPT_STATS */
#if defined(CONFIG_SMP) && XENO_DEBUG(NUCLEUS)
	remove_proc_entry("lock", rthal_proc_root);
#endif /* CONFIG_SMP && XENO_DEBUG(NUCLEUS) */
}

#ifdef CONFIG_XENO_OPT_PERVASIVE

static int iface_read_proc(char *page,
			   char **start,
			   off_t off, int count, int *eof, void *data)
{
	struct xnskentry *iface = (struct xnskentry *)data;
	int len, refcnt = xnarch_atomic_get(&iface->refcnt);

	len = sprintf(page, "%d\n", refcnt < 0 ? 0 : refcnt);

	len -= off;
	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;

	return len;
}

void xnpod_declare_iface_proc(struct xnskentry *iface)
{
	iface->proc = add_proc_leaf(iface->name,
				    &iface_read_proc, NULL, iface,
				    iface_proc_root);
}

void xnpod_discard_iface_proc(struct xnskentry *iface)
{
	remove_proc_entry(iface->name, iface_proc_root);
	iface->proc = NULL;
}

#endif /* CONFIG_XENO_OPT_PERVASIVE */

#endif /* CONFIG_PROC_FS && __KERNEL__ */

int __init __xeno_sys_init(void)
{
	int err;

	xnmod_sysheap_size = module_param_value(sysheap_size_arg) * 1024;

	nkmsgbuf = xnarch_sysalloc(XNPOD_FATAL_BUFSZ);

	if (!nkmsgbuf) {
		err = -ENOMEM;
		goto fail;
	}

	err = xnarch_init();

	if (err)
		goto fail;

#ifdef __KERNEL__
#ifdef CONFIG_PROC_FS
	xnpod_init_proc();
#endif /* CONFIG_PROC_FS */

	xnintr_mount();

#ifdef CONFIG_LTT
	xnltt_mount();
#endif /* CONFIG_LTT */

#ifdef CONFIG_XENO_OPT_PIPE
	err = xnpipe_mount();

	if (err)
		goto cleanup_arch;
#endif /* CONFIG_XENO_OPT_PIPE */

#ifdef CONFIG_XENO_OPT_PERVASIVE
	err = xnshadow_mount();

	if (err)
		goto cleanup_pipe;

	err = xnheap_mount();

	if (err)
		goto cleanup_shadow;

	err = xncore_mount();

	if (err)
		goto cleanup_heap;
#endif /* CONFIG_XENO_OPT_PERVASIVE */
#endif /* __KERNEL__ */

	xnloginfo("real-time nucleus v%s (%s) loaded.\n",
		  XENO_VERSION_STRING, XENO_VERSION_NAME);

	xeno_nucleus_status = 0;

	return 0;

#ifdef __KERNEL__

#ifdef CONFIG_XENO_OPT_PERVASIVE

	xncore_umount();

      cleanup_heap:

	xnheap_umount();

      cleanup_shadow:

	xnshadow_cleanup();

      cleanup_pipe:

#endif /* CONFIG_XENO_OPT_PERVASIVE */

#ifdef CONFIG_XENO_OPT_PIPE
	xnpipe_umount();

      cleanup_arch:

#endif /* CONFIG_XENO_OPT_PIPE */

#ifdef CONFIG_PROC_FS
	xnpod_delete_proc();
#endif /* CONFIG_PROC_FS */

	xnarch_exit();

#endif /* __KERNEL__ */

      fail:

	xnlogerr("system init failed, code %d.\n", err);

	xeno_nucleus_status = err;

	return err;
}

void __exit __xeno_sys_exit(void)
{
	xnpod_shutdown(XNPOD_NORMAL_EXIT);

#ifdef CONFIG_XENO_OPT_PERVASIVE
	/* Must take place before xnpod_delete_proc. */
	xnshadow_cleanup();
#endif /* CONFIG_XENO_OPT_PERVASIVE */

#if defined(__KERNEL__) &&  defined(CONFIG_PROC_FS)
	xnpod_delete_proc();
#endif /* __KERNEL__ && CONFIG_PROC_FS */

	xnarch_exit();

#ifdef __KERNEL__
#ifdef CONFIG_XENO_OPT_PERVASIVE
	xncore_umount();
	xnheap_umount();
#endif /* CONFIG_XENO_OPT_PERVASIVE */
#ifdef CONFIG_XENO_OPT_PIPE
	xnpipe_umount();
#endif /* CONFIG_XENO_OPT_PIPE */
#ifdef CONFIG_LTT
	xnltt_umount();
#endif /* CONFIG_LTT */
#endif /* __KERNEL__ */

	if (nkmsgbuf)
		xnarch_sysfree(nkmsgbuf, XNPOD_FATAL_BUFSZ);

	xnloginfo("real-time nucleus unloaded.\n");
}

EXPORT_SYMBOL(xnmod_glink_queue);
EXPORT_SYMBOL(xnmod_alloc_glinks);

module_init(__xeno_sys_init);
module_exit(__xeno_sys_exit);
