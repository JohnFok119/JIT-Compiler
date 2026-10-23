/**
 * Tony Givargis
 * Copyright (C), 2023-2025
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * loader.h
 */

#ifndef _JITC_H_
#define _JITC_H_

#include <stddef.h>
#include <stdio.h>
#include "ir/ir.h"
#include "jit.h"

struct jitc;

/**
 * Compiles the C program dir/expr.c into the dynamically loadable module
 * dir/expr.so by running
 *   gcc -shared -fPIC -O2 -ffp-contract=off -fno-builtin -o expr.so expr.c -lm
 * in a child process (working directory dir). -fno-builtin keeps gcc from
 * rewriting pow(x, 2) to x*x or folding libm calls itself, so every libm
 * call reaches glibc exactly as the interpreter's does.
 *
 * The compiler is "gcc" (found on PATH) unless the environment variable
 * JIT_GCC names another program or path. That exists so tests can point the
 * loader at /bin/false; do not set it in normal use.
 *
 * dir   : a directory that holds expr.c
 * errmsg: receives gcc's first line of stderr when compilation fails
 * errlen: size of errmsg
 *
 * return: 0 on success, otherwise error
 */

int jitc_compile(const char *dir, char *errmsg, size_t errlen);

/**
 * Loads a dynamically loadable module into the calling process' memory for
 * execution.
 *
 * pathname: the file pathname of the dynamically loadable module (it must
 *           contain a '/', otherwise dlopen() searches the library path)
 *
 * return: an opaque handle or NULL on error
 */

struct jitc *jitc_open(const char *pathname);

/**
 * Unloads a previously loaded dynamically loadable module.
 *
 * jitc: an opaque handle previously obtained by calling jitc_open()
 *
 * Note: jitc may be NULL
 */

void jitc_close(struct jitc *jitc);

/**
 * Searches for a symbol in the dynamically loaded module associated with jitc.
 *
 * jitc: an opaque handle previously obtained by calling jitc_open()
 *
 * return: the memory address of the start of the symbol, or 0 on error
 */

long jitc_lookup(struct jitc *jitc, const char *symbol);

/*
 * The GCC backend proper: IR -> C source -> gcc -> dlopen. The module lives
 * in a private temp directory (mkdtemp) that gcc_code_free() removes.
 */

struct gcc_code {
	char dir[32];             /* "/tmp/jit-XXXXXX", empty when not created */
	struct jitc *so;          /* loaded module, or NULL */
	void *fn;                 /* address of `evaluate` */
	char *src;                /* the generated C source (--emit c) */
	size_t srclen;
	int legacy;               /* evaluate(void) instead of evaluate(const double *) */
};

/* Generates, compiles and loads ir. 0, or -1 with err set (err->pos = -1;
 * on a gcc failure the message is gcc's first line of stderr). */
int    gcc_compile(const struct ir *, struct gcc_code *, struct jit_error *);
double gcc_call(const struct gcc_code *, const double *vars);
void   gcc_code_free(struct gcc_code *);
/* stage "c"; 0 ok, -1 unknown stage */
int    gcc_dump(const struct gcc_code *, const char *stage, FILE *);

#endif /* _JITC_H_ */
