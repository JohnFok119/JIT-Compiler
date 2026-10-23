/* W^X executable memory: the mapping is writable while the image is copied
 * in, then read + execute; it is never writable and executable at once. */

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "backend/x86_64/emit.h"

#ifdef __EMSCRIPTEN__
/* The browser build encodes x86 code only to show it (asm / hex stages) and
 * never runs it: plain heap memory, no mapping. */
void *
x86_exec_alloc(const void *image, size_t size)
{
	void *mem = malloc(size);

	if (mem) {
		memcpy(mem, image, size);
	}
	return mem;
}

void
x86_exec_free(void *mem, size_t size)
{
	(void)size;
	free(mem);
}
#else

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
#endif
