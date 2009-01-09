#ifndef _XENO_ASM_GENERIC_BITS_BIND_H
#define _XENO_ASM_GENERIC_BITS_BIND_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <nucleus/types.h>
#include <asm/xenomai/syscall.h>

#ifdef HAVE___THREAD
__thread __attribute__ ((tls_model ("initial-exec"), weak))
xnhandle_t xeno_current = XN_NO_HANDLE;
__thread __attribute__ ((tls_model ("initial-exec"), weak))
unsigned long xeno_current_mode;

static inline void __xeno_set_current(xnhandle_t current)
{
	xeno_current = current;
}
#else /* !HAVE___THREAD */
pthread_key_t xeno_current_key __attribute__ ((weak));
pthread_key_t xeno_current_mode_key __attribute__ ((weak));

static inline void __xeno_set_current(xnhandle_t current)
{
	pthread_setspecific(xeno_current_key, (void *)current);
}

__attribute__ ((weak))
unsigned long *xeno_init_current_mode(void)
{
	unsigned long *mode = malloc(sizeof(unsigned long));
	pthread_setspecific(xeno_current_mode_key, mode);
	return mode;
}

static void cleanup_current_mode(void *ptr)
{
	free(ptr);
}

static __attribute__ ((constructor))
void init_current_keys(void)
{
	int err = pthread_key_create(&xeno_current_key, NULL);
	if (err)
		goto error_exit;

	err = pthread_key_create(&xeno_current_mode_key, cleanup_current_mode);
	if (err) {
	  error_exit:
		fprintf(stderr, "Xenomai: error creating TSD key: %s\n",
			strerror(-err));
		exit(1);
	}
}
#endif /* !HAVE___THREAD */

__attribute__ ((weak))
void xeno_set_current(void)
{
	xnhandle_t current;
	int err;

	err = XENOMAI_SYSCALL1(__xn_sys_current, &current);
	if (err) {
		fprintf(stderr, "Xenomai: error obtaining handle for current "
			"thread: %s\n", strerror(-err));
		exit(1);
	}
	__xeno_set_current(current);
}

#ifdef CONFIG_XENO_FASTSYNCH
__attribute__ ((weak))
unsigned long xeno_sem_heap[2] = { 0, 0 };
#endif /* CONFIG_XENO_FASTSYNCH */

void xeno_handle_mlock_alert(int sig);

#ifdef CONFIG_XENO_FASTSYNCH
static void *map_sem_heap(unsigned shared)
{
	struct heap_info {
		void *addr;
		unsigned size;
	} hinfo;
	int fd, err;

#ifndef XENO_WRAPPED_OPEN
	fd = open("/dev/rtheap", O_RDWR, 0);
#else /* !XENO_WRAPPED_OPEN */
	fd = __real_open("/dev/rtheap", O_RDWR, 0);
#endif /* !XENO_WRAPPED_OPEN */
	if (fd < 0) {
		fprintf(stderr, "Xenomai: open: %m\n");
		return MAP_FAILED;
	}

	err = XENOMAI_SYSCALL2(__xn_sys_sem_heap, &hinfo, shared);
	if (err < 0) {
		fprintf(stderr, "Xenomai: sys_sem_heap: %m\n");
		return MAP_FAILED;
	}

	err = ioctl(fd, 0, hinfo.addr);
	if (err < 0) {
		fprintf(stderr, "Xenomai: ioctl: %m\n");
		return MAP_FAILED;
	}

	hinfo.addr = mmap(NULL, hinfo.size,
			  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);

	return hinfo.addr;
}

static void unmap_sem_heap(unsigned long heap_addr, unsigned shared)
{
	struct heap_info {
		void *addr;
		unsigned size;
	} hinfo;
	int err;

	err = XENOMAI_SYSCALL2(__xn_sys_sem_heap, &hinfo, shared);
	if (err < 0) {
		fprintf(stderr, "Xenomai: sys_sem_heap: %m\n");
		return;
	}

	munmap((void *) heap_addr, hinfo.size);
}
#endif /* CONFIG_XENO_FASTSYNCH */

void __attribute__((weak)) xeno_sigill_handler(int sig)
{
	fprintf(stderr, "Xenomai or CONFIG_XENO_OPT_PERVASIVE disabled.\n"
		"(modprobe xeno_nucleus?)\n");
	exit(1);
}

static inline int
xeno_bind_skin(unsigned skin_magic, const char *skin, const char *module)
{
	sighandler_t old_sigill_handler;
	struct sigaction sa;
	xnfeatinfo_t finfo;
	int muxid;

	old_sigill_handler = signal(SIGILL, xeno_sigill_handler);
	if (old_sigill_handler == SIG_ERR) {
		perror("signal(SIGILL)");
		exit(1);
	}

	muxid = XENOMAI_SYSBIND(skin_magic,
				XENOMAI_FEAT_DEP, XENOMAI_ABI_REV, &finfo);

	signal(SIGILL, old_sigill_handler);

	switch (muxid) {
	case -EINVAL:

		fprintf(stderr, "Xenomai: incompatible feature set\n");
		fprintf(stderr,
			"(userland requires \"%s\", kernel provides \"%s\", missing=\"%s\").\n",
			finfo.feat_man_s, finfo.feat_all_s, finfo.feat_mis_s);
		exit(1);

	case -ENOEXEC:

		fprintf(stderr, "Xenomai: incompatible ABI revision level\n");
		fprintf(stderr, "(needed=%lu, current=%lu).\n",
			XENOMAI_ABI_REV, finfo.abirev);
		exit(1);

	case -ENOSYS:
	case -ESRCH:

		fprintf(stderr,
			"Xenomai: %s skin or CONFIG_XENO_OPT_PERVASIVE disabled.\n"
			"(modprobe %s?)\n", skin, module);
		exit(1);
	}

	if (muxid < 0) {
		fprintf(stderr, "Xenomai: binding failed: %s.\n",
			strerror(-muxid));
		exit(1);
	}

#ifdef xeno_arch_features_check
	xeno_arch_features_check();
#endif /* xeno_arch_features_check */

	/* Install a SIGXCPU handler to intercept alerts about unlocked
	   process memory. */

	sa.sa_handler = &xeno_handle_mlock_alert;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGXCPU, &sa, NULL);

#ifdef CONFIG_XENO_FASTSYNCH
	/* In case we forked, we need to map the new local semaphore heap */
	if (xeno_sem_heap[0])
		unmap_sem_heap(xeno_sem_heap[0], 0);
	xeno_sem_heap[0] = (unsigned long) map_sem_heap(0);
	if (xeno_sem_heap[0] == (unsigned long) MAP_FAILED) {
		perror("Xenomai: mmap(local sem heap)");
		exit(EXIT_FAILURE);
	}

	/* Even if we forked the global semaphore heap did not change, no need
	  to map it anew */
	if (!xeno_sem_heap[1]) {
		xeno_sem_heap[1] = (unsigned long) map_sem_heap(1);
		if (xeno_sem_heap[1] == (unsigned long) MAP_FAILED) {
			perror("Xenomai: mmap(global sem heap)");
			exit(EXIT_FAILURE);
		}
	}
#endif /* CONFIG_XENO_FASTSYNCH */

	return muxid;
}

static inline int
xeno_bind_skin_opt(unsigned skin_magic, const char *skin, const char *module)
{
	sighandler_t old_sigill_handler;
	xnfeatinfo_t finfo;
	int muxid;

	old_sigill_handler = signal(SIGILL, xeno_sigill_handler);
	if (old_sigill_handler == SIG_ERR) {
		perror("signal(SIGILL)");
		exit(1);
	}

	muxid = XENOMAI_SYSBIND(skin_magic,
				XENOMAI_FEAT_DEP, XENOMAI_ABI_REV, &finfo);

	signal(SIGILL, old_sigill_handler);

	switch (muxid) {
	case -EINVAL:

		fprintf(stderr, "Xenomai: incompatible feature set\n");
		fprintf(stderr,
			"(userland requires \"%s\", kernel provides \"%s\", missing=\"%s\").\n",
			finfo.feat_man_s, finfo.feat_all_s, finfo.feat_mis_s);
		exit(1);

	case -ENOEXEC:

		fprintf(stderr, "Xenomai: incompatible ABI revision level\n");
		fprintf(stderr, "(needed=%lu, current=%lu).\n",
			XENOMAI_ABI_REV, finfo.abirev);
		exit(1);

	case -ENOSYS:
	case -ESRCH:

		return -1;
	}

	if (muxid < 0) {
		fprintf(stderr, "Xenomai: binding failed: %s.\n",
			strerror(-muxid));
		exit(1);
	}

#ifdef xeno_arch_features_check
	xeno_arch_features_check();
#endif /* xeno_arch_features_check */

#ifdef CONFIG_XENO_FASTSYNCH
	/* In case we forked, we need to map the new local semaphore heap */
	if (xeno_sem_heap[0])
		unmap_sem_heap(xeno_sem_heap[0], 0);
	xeno_sem_heap[0] = (unsigned long) map_sem_heap(0);
	if (xeno_sem_heap[0] == (unsigned long) MAP_FAILED) {
		perror("Xenomai: mmap(local sem heap)");
		exit(EXIT_FAILURE);
	}

	/* Even if we forked the global semaphore heap did not change, no need
	  to map it anew */
	if (!xeno_sem_heap[1]) {
		xeno_sem_heap[1] = (unsigned long) map_sem_heap(1);
		if (xeno_sem_heap[1] == (unsigned long) MAP_FAILED) {
			perror("Xenomai: mmap(global sem heap)");
			exit(EXIT_FAILURE);
		}
	}
#endif /* CONFIG_XENO_FASTSYNCH */

	return muxid;
}

#endif /* _XENO_ASM_GENERIC_BITS_BIND_H */
