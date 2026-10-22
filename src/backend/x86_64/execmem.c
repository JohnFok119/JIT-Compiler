/* W^X executable memory: the mapping is writable while the image is copied
 * in, then read + execute; it is never writable and executable at once. */

#include <string.h>
#include <sys/mman.h>
#include "backend/x86_64/emit.h"

void *
x86_exec_alloc(const void *image, size_t size)
{
	void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (MAP_FAILED == mem) {
		return NULL;
	}
	memcpy(mem, image, size);
	if (mprotect(mem, size, PROT_READ | PROT_EXEC)) {
		munmap(mem, size);
		return NULL;
	}
	return mem;
}

void
x86_exec_free(void *mem, size_t size)
{
	if (mem) {
		munmap(mem, size);
	}
}
