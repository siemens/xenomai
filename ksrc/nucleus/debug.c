/*!\file debug.c
 * \brief Debug services.
 * \author Philippe Gerum
 *
 * Copyright (C) 2010 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * \ingroup debug
 */
/*!
 * \ingroup nucleus
 * \defgroup debug Debugging services.
 *
 *@{*/

#include <linux/types.h>
#include <linux/limits.h>
#include <linux/ctype.h>
#include <linux/jhash.h>
#include <linux/mm.h>
#include <nucleus/pod.h>
#include <nucleus/debug.h>
#include <nucleus/heap.h>
#include <nucleus/sys_ppd.h>

#define SYMBOL_HSLOTS	(1 << 8)

struct hashed_symbol {
	struct hashed_symbol *next;
	char symbol[0];
};

static struct hashed_symbol *symbol_jhash[SYMBOL_HSLOTS];

/*
 * This is a permanent storage for ASCII strings which comes handy to
 * get a unique and constant reference to a symbol while preserving
 * storage space. Hashed symbols have infinite lifetime and are never
 * flushed.
 */
DEFINE_PRIVATE_XNLOCK(symbol_lock);

static const char *hash_symbol(const char *symbol)
{
	struct hashed_symbol *p, **h;
	const char *s;
	size_t len;
	u32 hash;

	len = strlen(symbol);
	hash = jhash(symbol, len, 0);

	xnlock_get(&symbol_lock);

	h = &symbol_jhash[hash & (SYMBOL_HSLOTS - 1)];
	p = *h;
	while (p &&
	       (*p->symbol != *symbol ||
		strcmp(p->symbol + 1, symbol + 1)))
	       p = p->next;

	if (p)
		goto done;

	p = xnmalloc(sizeof(*p) + len + 1);
	if (p == NULL) {
		s = NULL;
		goto out;
	}

	strcpy(p->symbol, symbol);
	p->next = *h;
	*h = p;
done:
	s = p->symbol;
out:
	xnlock_put(&symbol_lock);

	return s;
}

#ifdef CONFIG_XENO_OPT_DEBUG_TRACE_RELAX
/*
 * We define a static limit (RELAX_SPOTNR) for spot records to limit
 * the memory consumption (we pull record memory from the system
 * heap). The current value should be reasonable enough unless the
 * application is extremely unsane, given that we only keep unique
 * spots. Said differently, if the application has more than
 * RELAX_SPOTNR distinct code locations doing spurious relaxes, then
 * the first issue to address is likely PEBKAC.
 */
#define RELAX_SPOTNR	128
#define RELAX_HSLOTS	(1 << 8)
#define RELAX_CALLDEPTH	SIGSHADOW_BACKTRACE_DEPTH

struct relax_record {
	/* Number of hits for this location */
	u32 hits;
	struct relax_spot {
		/* Faulty thread name. */
		char thread[XNOBJECT_NAME_LEN];
		/* call stack the relax originates from. */
		int depth;
		struct backtrace {
			unsigned long pc;
			const char *mapname;
		} backtrace[RELAX_CALLDEPTH];
		/* Program hash value of the caller. */
		u32 proghash;
		/* Pid of the caller. */
		pid_t pid;
	} spot;
	struct relax_record *r_next;
	struct relax_record *h_next;
	const char *exe_path;
};

static struct relax_record *relax_jhash[RELAX_HSLOTS];

static struct relax_record *relax_record_list;

static int relax_overall, relax_queued;

DEFINE_PRIVATE_XNLOCK(relax_lock);

/*
 * The motivation to centralize tracing information about relaxes
 * directly into kernel space is fourfold:
 *
 * - it allows to gather all the trace data into a single location and
 * keep it safe there, with no external log file involved.
 *
 * - enabling the tracing does not impose any requirement on the
 * application (aside of being compiled with debug symbols for best
 * interpreting that information). We only need a kernel config switch
 * for this (i.e. CONFIG_XENO_OPT_DEBUG_TRACE_RELAX).
 *
 * - the data is collected and can be made available exactly the same
 * way regardless of the application emitting the relax requests, or
 * whether it is still alive when the trace data are displayed.
 *
 * - the kernel is able to provide accurate and detailed trace
 * information, such as the relative offset of instructions causing
 * relax requests within dynamic shared objects, without having to
 * guess it roughly from /proc/pid/maps, or relying on ldd's
 * --function-relocs feature, which both require to run on the target
 * system to get the needed information. Instead, we allow a build
 * host to use a cross-compilation toolchain later to extract the
 * source location, from the raw data the kernel has provided on the
 * target system.
 *
 * However, collecting the call frames within the application to
 * determine the full context of a relax spot is not something we can
 * do purely from kernel space, notably because it depends on build
 * options we just don't know about (e.g. frame pointers availability
 * for the app, or other nitty-gritty details depending on the
 * toolchain). To solve this, we ask the application to send us a
 * complete backtrace taken from the context of a specific signal
 * handler, which we know is stacked over the relax spot. That
 * information is then stored by the kernel after some
 * post-processing, along with other data identifying the caller, and
 * made available through the /proc/xenomai/debug/relax vfile.
 *
 * Implementation-wise, xndebug_notify_relax and xndebug_trace_relax
 * routines are paired: first, xndebug_notify_relax sends a SIGSHADOW
 * request to userland when a relax spot is detected from
 * xnshadow_relax, which should then trigger a call back to
 * xndebug_trace_relax with the complete backtrace information, as
 * seen from userland (via the internal __xn_sys_backtrace
 * syscall). All this is ran on behalf of the relaxing thread, so we
 * can make a number of convenient assumptions (such as being able to
 * scan the current vma list to get detailed information about the
 * executable mappings that could be involved).
 */

void xndebug_notify_relax(struct xnthread *thread)
{
	xnshadow_send_sig(thread, SIGSHADOW,
			  SIGSHADOW_ACTION_BACKTRACE,
			  1);
}

void xndebug_trace_relax(int nr, unsigned long __user *u_backtrace)
{
	unsigned long backtrace[RELAX_CALLDEPTH];
	struct relax_record *p, **h;
	struct vm_area_struct *vma;
	struct xnthread *thread;
	struct relax_spot spot;
	struct mm_struct *mm;
	struct file *file;
	unsigned long pc;
	char *mapname;
	int n, depth;
	char *tmp;
	u32 hash;

	thread = xnshadow_thread(current);
	if (thread == NULL)
		return;		/* Can't be, right? What a mess. */
	/*
	 * In case backtrace() in userland is broken or fails. We may
	 * want to know about this in kernel space however, for future
	 * use.
	 */
	if (nr <= 0)
		return;
	/*
	 * We may omit the older frames if we can't store the full
	 * backtrace.
	 */
	if (nr > RELAX_CALLDEPTH)
		nr = RELAX_CALLDEPTH;
	/*
	 * Fetch the backtrace array, filled with PC values as seen
	 * from the relaxing thread in user-space. This can't fail
	 */
	if (__xn_safe_copy_from_user(backtrace, u_backtrace, nr * sizeof(pc)))
		return;
	/*
	 * We compute PC values relative to the base of the shared
	 * executable mappings we find in the backtrace, which makes
	 * it possible for the slackspot utility to match the
	 * corresponding source code locations from unrelocated file
	 * offsets. Note that we don't translate PC values within pure
	 * executable vmas.
	 */

	tmp = (char *)__get_free_page(GFP_TEMPORARY);
	if (tmp == NULL)
		/*
		 * The situation looks really bad, but we can't do
		 * anything about it. Just bail out.
		 */
		return;

	memset(&spot, 0, sizeof(spot));
	mm = get_task_mm(current);
	down_read(&mm->mmap_sem);

	for (n = 0, depth = 0; n < nr; n++) {
		pc = backtrace[n];

		vma = find_vma(mm, pc);
		if (vma == NULL)
			continue;

		if (!(vma->vm_flags & VM_EXECUTABLE))
			pc -= vma->vm_start;

		spot.backtrace[depth].pc = pc;

		/*
		 * Even in case we can't fetch the map name, we still
		 * record the PC value, which may still give some hint
		 * downstream.
		 */
		file = vma->vm_file;
		if (file == NULL)
			goto next_frame;

		mapname = d_path(&file->f_path, tmp, PAGE_SIZE);
		if (IS_ERR(mapname))
			goto next_frame;

		spot.backtrace[depth].mapname = hash_symbol(mapname);
	next_frame:
		depth++;
	}

	up_read(&mm->mmap_sem);
	mmput(mm);
	free_page((unsigned long)tmp);

	/*
	 * Most of the time we will be sent duplicates, since the odds
	 * of seeing the same thread running the same code doing the
	 * same mistake all over again are high. So we probe the hash
	 * table for an identical spot first, before going for a
	 * complete record allocation from the system heap if no match
	 * was found. Otherwise, we just take the fast exit path.
	 */
	spot.depth = depth;
	spot.proghash = thread->proghash;
	spot.pid = xnthread_user_pid(thread);
	strcpy(spot.thread, thread->name);
	hash = jhash2((u32 *)&spot, sizeof(spot) / sizeof(u32), 0);

	xnlock_get(&relax_lock);

	h = &relax_jhash[hash & (RELAX_HSLOTS - 1)];
	p = *h;
	while (p &&
	       /* Try quick guesses first, then memcmp */
	       (p->spot.depth != spot.depth ||
		p->spot.pid != spot.pid ||
		memcmp(&p->spot, &spot, sizeof(spot))))
	       p = p->h_next;

	if (p) {
		p->hits++;
		goto out;	/* Spot already recorded. */
	}

	if (relax_queued >= RELAX_SPOTNR)
		goto out;	/* No more space -- ignore. */
	/*
	 * We can only compete with other shadows which have just
	 * switched to secondary mode like us. So holding the
	 * relax_lock a bit more without disabling interrupts is not
	 * an issue. This allows us to postpone the record memory
	 * allocation while probing and updating the hash table in a
	 * single move.
	 */
	p = xnmalloc(sizeof(*p));
	if (p == NULL)
		goto out;      /* Something is about to go wrong... */

	memcpy(&p->spot, &spot, sizeof(p->spot));
	p->exe_path = hash_symbol(thread->exe_path);
	p->hits = 1;
	p->h_next = *h;
	*h = p;
	p->r_next = relax_record_list;
	relax_record_list = p;
	relax_queued++;
out:
	relax_overall++;

	xnlock_put(&relax_lock);
}

static DEFINE_VFILE_HOSTLOCK(relax_mutex);

struct relax_vfile_priv {
	int queued;
	int overall;
	int ncurr;
	struct relax_record *head;
	struct relax_record *curr;
};

static void *relax_vfile_begin(struct xnvfile_regular_iterator *it)
{
	struct relax_vfile_priv *priv = xnvfile_iterator_priv(it);
	struct relax_record *p;
	int n;

	/*
	 * Snapshot the counters under lock, to make sure they remain
	 * mutually consistent despite we dump the record list in a
	 * lock-less manner. Additionally, the vfile layer already
	 * holds the relax_mutex lock for us, so that we can't race
	 * with ->store().
	 */
	xnlock_get(&relax_lock);

	if (it->pos >= relax_queued) {
		xnlock_put(&relax_lock);
		return NULL;
	}
	priv->overall = relax_overall;
	priv->queued = relax_queued;
	priv->head = relax_record_list;

	xnlock_put(&relax_lock);

	if (it->pos == 0) {
		priv->curr = NULL;
		priv->ncurr = -1;
		return VFILE_SEQ_START;
	}

	for (n = 1, p = priv->head; n < it->pos; n++)
		p = p->r_next;

	priv->curr = p;
	priv->ncurr = n;

	return p;
}

static void *relax_vfile_next(struct xnvfile_regular_iterator *it)
{
	struct relax_vfile_priv *priv = xnvfile_iterator_priv(it);
	struct relax_record *p;
	int n;

	if (it->pos > priv->queued)
		return NULL;

	if (it->pos == priv->ncurr + 1)
		p = priv->curr->r_next;
	else {
		for (n = 1, p = priv->head; n < it->pos; n++)
			p = p->r_next;
	}

	priv->curr = p;
	priv->ncurr = it->pos;

	return p;
}

static int relax_vfile_show(struct xnvfile_regular_iterator *it, void *data)
{
	struct relax_vfile_priv *priv = xnvfile_iterator_priv(it);
	struct relax_record *p = data;
	int n;

	/*
	 * No need to grab any lock to read a record from a previously
	 * validated index: the data must be there and won't be
	 * touched anymore.
	 */
	if (p == NULL) {
		xnvfile_printf(it, "%d\n", priv->overall);
		return 0;
	}

	xnvfile_printf(it, "%s\n", p->exe_path ?: "?");
	xnvfile_printf(it, "%d %d %s\n", p->spot.pid, p->hits,
		       p->spot.thread);

	for (n = 0; n < p->spot.depth; n++)
		xnvfile_printf(it, "0x%lx %s\n",
			       p->spot.backtrace[n].pc,
			       p->spot.backtrace[n].mapname ?: "?");

	xnvfile_printf(it, ".\n");
		
	return 0;
}

static ssize_t relax_vfile_store(struct xnvfile_input *input)
{
	struct relax_record *p, *np;

	/*
	 * Flush out all records. Races with ->show() are prevented
	 * using the relax_mutex lock. The vfile layer takes care of
	 * this internally.
	 */
	xnlock_get(&relax_lock);
	p = relax_record_list;
	relax_record_list = NULL;
	relax_overall = 0;
	relax_queued = 0;
	xnlock_put(&relax_lock);

	while (p) {
		np = p->r_next;
		xnfree(p);
		p = np;
	}

	return input->size;
}

static struct xnvfile_regular_ops relax_vfile_ops = {
	.begin = relax_vfile_begin,
	.next = relax_vfile_next,
	.show = relax_vfile_show,
	.store = relax_vfile_store,
};

static struct xnvfile_regular relax_vfile = {
	.privsz = sizeof(struct relax_vfile_priv),
	.ops = &relax_vfile_ops,
	.entry = { .lockops = &relax_mutex.ops },
};

static inline int init_trace_relax(void)
{
	return xnvfile_init_regular("relax", &relax_vfile, &debug_vfroot);
}

static inline void cleanup_trace_relax(void)
{
	xnvfile_destroy_regular(&relax_vfile);
}

#else /* !CONFIG_XENO_OPT_DEBUG_TRACE_RELAX */

static inline int init_trace_relax(void)
{
	return 0;
}

static inline void cleanup_trace_relax(void)
{
}

static inline void init_thread_relax_trace(struct xnthread *thread)
{
}

#endif /* !XENO_OPT_DEBUG_TRACE_RELAX */

void xndebug_shadow_init(struct xnthread *thread)
{
	struct xnsys_ppd *sys_ppd;
	size_t len;

	xnlock_get(&nklock);
	sys_ppd = xnsys_ppd_get(0);
	xnlock_put(&nklock);
	/*
	 * The caller is current, so we know for sure that sys_ppd
	 * will still be valid after we dropped the lock.
	 */
	thread->exe_path = sys_ppd->exe_path ?: "(unknown)";
	/*
	 * The program hash value is a unique token debug features may
	 * use to identify all threads which belong to a given
	 * executable file. Using this value for quick probes is often
	 * handier and more efficient than testing the whole exe_path.
	 */
	len = strlen(thread->exe_path);
	thread->proghash = jhash(thread->exe_path, len, 0);
}

int xndebug_init(void)
{
	int ret;

	ret = init_trace_relax();
	if (ret)
		return ret;

	return 0;
}

void xndebug_cleanup(void)
{
	cleanup_trace_relax();
}

/*@}*/
