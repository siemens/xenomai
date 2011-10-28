#ifndef APC_H
#define APC_H

#define COBALT_LO_SIGNAL_REQ 0
#define COBALT_LO_FREE_REQ   1

void cobalt_schedule_lostage(int request, void *arg, size_t size);

int cobalt_apc_pkg_init(void);

void cobalt_apc_pkg_cleanup(void);

#endif /* APC_H */
