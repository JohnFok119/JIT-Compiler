/**
 * Tony Givargis
 * Copyright (C), 2023-2025
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * loader.c
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dlfcn.h>
#include "system.h"
#include "backend/gcc/cgen.h"
#include "backend/gcc/loader.h"

/**
 * Needs:
 *   fork()
 *   execv()
 *   waitpid()
 *   WIFEXITED()
 *   WEXITSTATUS()
 *   dlopen()
 *   dlclose()
 *   dlsym()
 */

/* research the above Needed API and design accordingly */

/* Copies the first line of buf into msg (no newline). */
static void
first_line(char *msg, size_t len, const char *buf)
{
    size_t n = strcspn(buf, "\n");

    if (n >= len) {
        n = len - 1;
    }
    memcpy(msg, buf, n);
    msg[n] = '\0';
}

int jitc_compile(const char *dir, char *errmsg, size_t errlen) {
    int fds[2];
    pid_t pid;
    int status;
    char buf[1024], drain[256];
    size_t used = 0;
    ssize_t got;

    errmsg[0] = '\0';

    /* A pipe carries gcc's stderr back to us: the child's end is dup2()ed
       onto file descriptor 2 before gcc starts. */

    if (pipe(fds)) {
        snprintf(errmsg, errlen, "pipe() failed");
        return -1;
    }
    pid = fork();

    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        snprintf(errmsg, errlen, "Failed to create child process");
        return(-1);
    }
    else if (pid == 0) {
        /* --- Child Process --- */
        const char *args[] = {"gcc", "-shared", "-fPIC", "-O2",
                              "-ffp-contract=off", "-o", "expr.so",
                              "expr.c", "-lm", NULL};

        close(fds[0]);
        dup2(fds[1], 2);
        close(fds[1]);

        /* Work inside the temp directory so gcc's messages say "expr.c:". */

        if (chdir(dir)) {
            fprintf(stderr, "chdir() failed\n");
            _exit(127);
        }

        /* execv() replaces the child process with gcc.
           The second argument is cast to '(char * const *)' to match
           the parameter of execv. */

        execv("/usr/bin/gcc", (char * const *)args);

        /* If execv returns, it means an error occurred.
           We must exit the child process to prevent it from continuing. */

        fprintf(stderr, "execv() failed\n");
        _exit(127);
    }
    else {
        /* --- Parent Process --- */

        close(fds[1]);

        /* Read everything gcc says before waiting (keeping the start), so a
           chatty gcc can never block on a full pipe. */

        while (0 < (got = read(fds[0], used < sizeof (buf) - 1 ?
                               buf + used : drain,
                               used < sizeof (buf) - 1 ?
                               sizeof (buf) - 1 - used : sizeof (drain)))) {
            if (used < sizeof (buf) - 1) {
                used += (size_t)got;
            }
        }
        buf[used] = '\0';
        close(fds[0]);

        /* Wait for child process by calling waitpid() */

        waitpid(pid, &status, 0);

        /* Check if the child terminated normally. If so, check its exit code.
           A non-zero exit code from gcc indicates a compilation error. */

        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == 0) {
                return 0;
            }
            else {
                first_line(errmsg, errlen, buf);
                if (!errmsg[0]) {
                    snprintf(errmsg, errlen, "gcc failed (exit %d)",
                             WEXITSTATUS(status));
                }
                return -1;
            }
        }

        snprintf(errmsg, errlen,
                 "compilation failed or child terminated abnormally");
        return -1;
    }
}

struct jitc *jitc_open(const char *pathname) {
    return dlopen(pathname, RTLD_NOW | RTLD_LOCAL);
}

void jitc_close(struct jitc *jitc) {

    /* We make sure that jitc is not NULL here before performing dlclose() */

    if (jitc) {
        dlclose(jitc);
    }
}

long jitc_lookup(struct jitc *jitc, const char *symbol) {
    return (long)dlsym(jitc, symbol);
}

/* ---- the GCC backend: generate, compile, load, call, clean up ---- */

typedef double (*evaluate_t)(const double *);
typedef double (*evaluate_legacy_t)(void);

static int
fail(struct jit_error *err, const char *msg)
{
    err->pos = -1;
    snprintf(err->msg, sizeof (err->msg), "%s", msg);
    return -1;
}

int
gcc_compile(const struct ir *ir, struct gcc_code *g, struct jit_error *err)
{
    char path[64], msg[sizeof (err->msg)];
    FILE *mem, *file;
    long addr;
    int bad;

    memset(g, 0, sizeof (*g));
    g->legacy = ir->legacy;
    if (!(mem = open_memstream(&g->src, &g->srclen))) {
        return fail(err, "out of memory");
    }
    bad = ir->legacy ? cgen_legacy(ir, mem) : cgen_normal(ir, mem);
    if (fclose(mem) || bad) {
        gcc_code_free(g);
        return fail(err, "cannot generate C for this expression");
    }

    /* a private directory: nobody else can swap expr.c / expr.so under us */
    snprintf(g->dir, sizeof (g->dir), "/tmp/jit-XXXXXX");
    if (!mkdtemp(g->dir)) {
        g->dir[0] = '\0';
        gcc_code_free(g);
        return fail(err, "cannot create a temporary directory");
    }
    snprintf(path, sizeof (path), "%s/expr.c", g->dir);
    if (!(file = fopen(path, "w"))) {
        gcc_code_free(g);
        return fail(err, "cannot write the C source");
    }
    if (fwrite(g->src, 1, g->srclen, file) != g->srclen || fclose(file)) {
        gcc_code_free(g);
        return fail(err, "cannot write the C source");
    }
    if (jitc_compile(g->dir, msg, sizeof (msg))) {
        gcc_code_free(g);
        return fail(err, msg);
    }
    snprintf(path, sizeof (path), "%s/expr.so", g->dir);
    if (!(g->so = jitc_open(path)) ||
        !(addr = jitc_lookup(g->so, "evaluate"))) {
        gcc_code_free(g);
        return fail(err, "cannot load the compiled expression");
    }
    g->fn = (void *)addr;
    return 0;
}

double
gcc_call(const struct gcc_code *g, const double *vars)
{
    /* object pointer -> function pointer without a pedantic cast */
    evaluate_t f;
    evaluate_legacy_t lf;

    if (g->legacy) {
        memcpy(&lf, &g->fn, sizeof (lf));
        return lf();
    }
    memcpy(&f, &g->fn, sizeof (f));
    return f(vars);
}

void
gcc_code_free(struct gcc_code *g)
{
    char path[64];

    jitc_close(g->so);
    if (g->dir[0]) {
        snprintf(path, sizeof (path), "%s/expr.so", g->dir);
        file_delete(path);
        snprintf(path, sizeof (path), "%s/expr.c", g->dir);
        file_delete(path);
        if (rmdir(g->dir)) {
            /* ignore */
        }
    }
    free(g->src);
    memset(g, 0, sizeof (*g));
}

int
gcc_dump(const struct gcc_code *g, const char *stage, FILE *out)
{
    if (strcmp(stage, "c")) {
        return -1;
    }
    fwrite(g->src, 1, g->srclen, out);
    return 0;
}
