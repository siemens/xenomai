#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "check.h"

#define SHM_NAME "/shm"
#define SHM_SZ 16384

int main(void)
{
	unsigned i;
	void *shm;
	int fd;

	fprintf(stderr, "Checking posix skin shared memories\n");

	fd = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_EXCL, 0644);
	if (fd == -1 && errno == EEXIST) {
		fprintf(stderr, "Removing previous shared memory\n");
		check_unix(shm_unlink(SHM_NAME));
		check_unix(fd = shm_open(SHM_NAME,
					 O_RDWR | O_CREAT | O_EXCL, 0644));
	}
	check_unix(ftruncate(fd, SHM_SZ));
	shm = mmap(NULL, SHM_SZ, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	check_unix(shm == MAP_FAILED ? -1 : 0);
	for (i = 0; i < SHM_SZ; i++)
		if (((unsigned char *)shm)[i] != 0) {
			fprintf(stderr, "Test 1 failed at byte %u\n", i);
			check_unix(shm_unlink(SHM_NAME));
			exit(EXIT_FAILURE);
		}

	/* Fill the shared memory */
	memset(shm, 0xA5, SHM_SZ);
	check_unix(munmap(shm, SHM_SZ));
	check_unix(close(fd));

	check_unix(fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0644));

	/* Resize it */
	check_unix(ftruncate(fd, 2 * SHM_SZ));
	shm = mmap(NULL, 2 * SHM_SZ, PROT_READ, MAP_SHARED, fd, 0);
	check_unix(shm == MAP_FAILED ? -1 : 0);

	/* Check contents */
	for (i = 0; i < SHM_SZ; i++)
		if (((unsigned char *)shm)[i] != 0xA5) {
			fprintf(stderr, "Test 2 failed at byte %u (%x)\n",
				i, ((unsigned char *)shm)[i]);
			check_unix(shm_unlink(SHM_NAME));
			exit(EXIT_FAILURE);
		}
	for (i = SHM_SZ; i < 2 * SHM_SZ; i++)
		if (((unsigned char *)shm)[i] != 0) {
			fprintf(stderr, "Test 2 failed at byte %u (%x)\n",
				i, ((unsigned char *)shm)[i]);
			check_unix(shm_unlink(SHM_NAME));
			exit(EXIT_FAILURE);
		}
	check_unix(munmap(shm, 2 * SHM_SZ));
	check_unix(close(fd));
	check_unix(shm_unlink(SHM_NAME));

	fprintf(stderr, "posix skin shared memories: success\n");
	return EXIT_SUCCESS;
}
