/**
 * Tony Givargis
 * Copyright (C), 2023-2025
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * jitc.c
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dlfcn.h>
#include "system.h"
#include "jitc.h"

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

int jitc_compile(const char *input, const char *output) {
    pid_t pid = fork();
    int status;

    if (pid < 0) {
        fprintf(stderr, "Failed to create child process\n");
        return(-1);
    } 
    else if (pid == 0) {
        /* --- Child Process --- */
        const char *args[] = {"gcc", "-shared", "-fPIC", "-o", NULL, NULL, "-lm", NULL};

        args[4] = output;
        args[5] = input;

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

        /* Wait for child process by calling waitpid() */

        waitpid(pid, &status, 0);

        /* Check if the child terminated normally. If so, check its exit code.
           A non-zero exit code from gcc indicates a compilation error. */

        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == 0) {
                return 0;
            }
            else {
                fprintf(stderr, "waitpid() failed\n");
                return -1;
            }
        }

        fprintf(stderr, "compilation failed or child terminated abnormally\n");
        return -1;
    }
}

struct jitc *jitc_open(const char *pathname) {
    return dlopen(pathname, RTLD_LAZY);
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
