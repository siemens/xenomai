#include <xenomai/native/heap.h>

RT_HEAP heap_desc;

void *shared_mem; /* Start address of the shared memory segment */

/* A shared memory segment with Xenomai is implemented as a shared
   real-time heap object. In this variant, the allocation routine
   always returns the start address of the heap memory to all callers,
   and the free routine always leads to a no-op. */

int main (int argc, char *argv[])

{
    int err;

    /* Bind to a shared heap which has been created elsewhere, either
       in kernel or user-space. The call will block us until such heap
       is created with the expected name. The heap should have been
       created with the H_SHARED mode set, which is implicit when
       creation takes place in user-space. */

    err = rt_heap_bind(&heap_desc,"SomeShmName",TM_NONBLOCK);

    if (err)
	fail();

    /* Get the address of the shared memory segment. The "size" and
       "timeout" arguments are unused here. */
    rt_heap_alloc(&heap_desc,0,TM_NONBLOCK,&shared_mem);

    /* ... */
}

void cleanup (void)

{
    /* We need to unbind explicitely from the heap in order to
       properly release the underlying memory mapping. Exiting the
       process unbinds all mappings automatically. */
    rt_heap_unbind(&heap_desc);
}
