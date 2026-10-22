/* --bench: time compile and N evaluations on every backend. */

#ifndef CLI_BENCH_H
#define CLI_BENCH_H

/* Prints the table to stdout; 0 ok, 1 error. */
int bench_run(const char *src, long n);

#endif /* CLI_BENCH_H */
