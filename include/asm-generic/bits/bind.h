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
#include <asm/xenomai/syscall.h>

__attribute__ ((weak))
pthread_key_t xeno_current_key;
__attribute__ ((weak))
pthread_once_t xeno_init_current_key_once = PTHREAD_ONCE_INIT;

__attribute__ ((weak))
void xeno_set_current(void)
{
	void *kthread_cb;
	XENOMAI_SYSCALL1(__xn_sys_current, &kthread_cb);
	pthread_setspecific(xeno_current_key, kthread_cb);
}

static void init_current_key(void)
{
	int err = pthread_key_create(&xeno_current_key, NULL);
	if (err) {
		fprintf(stderr, "Xenomai: error creating TSD key: %s\n",
			strerror(err));
		exit(1);
	}
}

#ifdef CONFIG_XENO_FASTSEM
__attribute__ ((weak))
unsigned long xeno_sem_heap[2] = { 0, 0 };
#endif /* CONFIG_XENO_FASTSEM */

void xeno_handle_mlock_alert(int sig);

#ifdef CONFIG_XENO_FASTSEM
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
#endif /* CONFIG_XENO_FASTSEM */

static inline int
xeno_bind_skin(unsigned skin_magic, const char *skin, const char *module)
{
	struct sigaction sa;
	xnfeatinfo_t finfo;
	int muxid;

	muxid = XENOMAI_SYSBIND(skin_magic,
				XENOMAI_FEAT_DEP, XENOMAI_ABI_REV, &finfo);
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

	pthread_once(&xeno_init_current_key_once, &init_current_key);

#ifdef CONFIG_XENO_FASTSEM
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
#endif /* CONFIG_XENO_FASTSEM */

	return muxid;
}

static inline int
xeno_bind_skin_opt(unsigned skin_magic, const char *skin, const char *module)
{
	xnfeatinfo_t finfo;
	int muxid;

	muxid = XENOMAI_SYSBIND(skin_magic,
				XENOMAI_FEAT_DEP, XENOMAI_ABI_REV, &finfo);
	switch (muxid) {
	case -EINVAL:

		fprintf(stderr, "Xenomai: incompatible feature set\n");
		fprintf(stderr,
			"(required=\"%s\", present=\"%s\", missing=\"%s\").\n",
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

	pthread_once(&xeno_init_current_key_once, &init_current_key);

#ifdef CONFIG_XENO_FASTSEM
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
#endif /* CONFIG_XENO_FASTSEM */

	return muxid;
}

#endif /* _XENO_ASM_GENERIC_BITS_BIND_H */
