#ifndef _XENO_NUCLEUS_BIND_H
#define _XENO_NUCLEUS_BIND_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <asm/xenomai/syscall.h>

__attribute__((weak)) int xeno_sigxcpu_no_mlock = 1;

static void  xeno_handle_mlock_alert (int sig)

{
    if (xeno_sigxcpu_no_mlock)
        {
        fprintf(stderr,"Xenomai: process memory not locked (missing mlockall?)\n");
        fflush(stderr);
        exit(4);
        }
    else
	{
	/* XNTRAPSW was set for the thread but no user-defined
	   handler has been set to override our internal handler, so
	   let's invoke the default signal action. */
        struct sigaction sa;

	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGXCPU,&sa,NULL);
	pthread_kill(pthread_self(),SIGXCPU);
	}
}

static inline int
xeno_user_skin_init(unsigned skin_magic, const char *skin, const char *module)
{
    struct sigaction sa;
    xnfeatinfo_t finfo;
    int muxid;

#ifdef xeno_arch_features_check
    xeno_arch_features_check();
#endif /* xeno_arch_features_check */
    
    muxid = XENOMAI_SYSBIND(skin_magic,
			    XENOMAI_FEAT_DEP,
			    XENOMAI_ABI_REV,
			    &finfo);
    switch (muxid)
	{
	case -EINVAL:

	    fprintf(stderr,"Xenomai: incompatible feature set\n");
	    fprintf(stderr,"(required=\"%s\", present=\"%s\", missing=\"%s\").\n",
		    finfo.feat_man_s,finfo.feat_all_s,finfo.feat_mis_s);
	    exit(1);

	case -ENOEXEC:

	    fprintf(stderr,"Xenomai: incompatible ABI revision level\n");
	    fprintf(stderr,"(needed=%lu, current=%lu).\n",
		    XENOMAI_ABI_REV,finfo.abirev);
	    exit(1);

	case -ENOSYS:
	case -ESRCH:

	    fprintf(stderr,"Xenomai: %s skin or CONFIG_XENO_OPT_PERVASIVE disabled.\n"
                    "(modprobe %s?)\n", skin, module);
	    exit(1);
        }
    
    if (muxid < 0)
        {
        fprintf(stderr,"Xenomai: binding failed: %s.\n",strerror(-muxid));
        exit(1);
        }

    /* Install a SIGXCPU handler to intercept alerts about unlocked
       process memory. */

    sa.sa_handler = &xeno_handle_mlock_alert;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGXCPU,&sa,NULL);

    return muxid;
}

#endif /* _XENO_NUCLEUS_BIND_H */
