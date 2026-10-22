/* Command-line front end: jit [options] '<expr>' [name=value ...] */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jit.h"
#include "api.h"
#include "backend/gcc/cgen.h"
#include "ir/ir.h"
#include "jitc.h"
#include "system.h"

typedef double (*evaluate_t)(void);

static int
usage(void)
{
	fprintf(stderr,
		"usage: jit [--legacy] [--emit tokens|ast|ir] "
		"[--backend interp|gcc] '<expr>' [name=value ...]\n");
	return 2;
}

static int
is_letter(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int
print_parse_error(const char *src, const struct jit_error *err)
{
	int i;

	fprintf(stderr, "error: %s\n", err->msg);
	if (0 <= err->pos) {
		fprintf(stderr, "  %s\n  ", src);
		for (i = 0; i < err->pos && src[i]; ++i) {
			fputc('\t' == src[i] ? '\t' : ' ', stderr);
		}
		fprintf(stderr, "^\n");
	}
	return 1;
}

/* Original assignment path: C source -> gcc -shared -> dlopen -> call. */
static int
run_legacy_gcc(const jit_expr *e)
{
	const char *SOFILE = "./out.so";
	const char *CFILE = "out.c";
	struct jitc *jitc;
	evaluate_t fnc;
	FILE *file;
	int bad;

	if (!(file = fopen(CFILE, "w"))) {
		fprintf(stderr, "error: cannot write %s\n", CFILE);
		return 1;
	}
	bad = cgen_legacy(&e->ir, file);
	if (fclose(file) || bad) {
		file_delete(CFILE);
		fprintf(stderr, "error: cannot generate C for this expression\n");
		return 1;
	}
	if (jitc_compile(CFILE, SOFILE)) {
		file_delete(CFILE);
		fprintf(stderr, "error: gcc failed to compile the expression\n");
		return 1;
	}
	file_delete(CFILE);
	if (!(jitc = jitc_open(SOFILE)) ||
	    !(fnc = (evaluate_t)jitc_lookup(jitc, "evaluate"))) {
		file_delete(SOFILE);
		jitc_close(jitc);
		fprintf(stderr, "error: cannot load the compiled expression\n");
		return 1;
	}
	printf("%f\n", fnc());
	file_delete(SOFILE);
	jitc_close(jitc);
	return 0;
}

static int
run(int argc, char *argv[], jit_expr **pe)
{
	const char *emit = NULL, *src;
	int legacy = 0, backend = -1, i = 1, j, k;
	double vals[JIT_MAX_VARS] = { 0 };
	int have[JIT_MAX_VARS] = { 0 };
	struct jit_error err;
	char buf[32];
	jit_expr *e;

	/* options: exact strings, only before the expression */
	while (i < argc) {
		const char *a = argv[i];

		if (!strcmp(a, "--legacy")) {
			legacy = 1;
			++i;
		}
		else if (!strcmp(a, "--emit")) {
			if (i + 1 >= argc ||
			    (strcmp(argv[i + 1], "tokens") &&
			     strcmp(argv[i + 1], "ast") &&
			     strcmp(argv[i + 1], "ir"))) {
				return usage();
			}
			emit = argv[i + 1];
			i += 2;
		}
		else if (!strcmp(a, "--backend")) {
			const char *v = i + 1 < argc ? argv[i + 1] : "";

			if (!strcmp(v, "interp")) {
				backend = JIT_INTERP;
			}
			else if (!strcmp(v, "gcc")) {
				backend = JIT_GCC;
			}
			else if (!strcmp(v, "x86")) {
				backend = JIT_X86;
			}
			else if (!strcmp(v, "wasm")) {
				backend = JIT_WASM;
			}
			else {
				return usage();
			}
			i += 2;
		}
		else if ('-' == a[0] && '-' == a[1] && is_letter(a[2])) {
			return usage();
		}
		else {
			break;
		}
	}
	if (i >= argc) {
		return usage();
	}
	src = argv[i++];
	for (j = i; j < argc; ++j) {
		const char *eq = strchr(argv[j], '=');
		if (!eq || eq == argv[j]) {
			return usage();
		}
	}

	if (!(*pe = e = jit_parse(src, legacy, &err))) {
		return print_parse_error(src, &err);
	}

	/* name=value arguments; a repeated name: the last value wins */
	for (j = i; j < argc; ++j) {
		const char *eq = strchr(argv[j], '=');
		int len = (int)(eq - argv[j]);
		char *end;
		double v;

		for (k = 0; k < jit_nvars(e); ++k) {
			const char *name = jit_var_name(e, k);
			if ((int)strlen(name) == len &&
			    !memcmp(name, argv[j], (size_t)len)) {
				break;
			}
		}
		if (k == jit_nvars(e)) {
			fprintf(stderr,
				"error: '%.*s' is not used in the expression\n",
				len, argv[j]);
			return 1;
		}
		v = strtod(eq + 1, &end);
		if (!eq[1] || *end) {
			fprintf(stderr, "error: bad value for '%.*s'\n",
				len, argv[j]);
			return 1;
		}
		vals[k] = v;
		have[k] = 1;
	}

	if (emit) {
		jit_dump(e, emit, stdout);
		return 0;
	}

	for (k = 0; k < jit_nvars(e); ++k) {
		if (!have[k]) {
			fprintf(stderr, "error: no value for variable '%s'\n",
				jit_var_name(e, k));
			return 1;
		}
	}

	if (0 > backend) {
		backend = legacy ? JIT_GCC : JIT_INTERP;
	}
	if (legacy && (JIT_X86 == backend || JIT_WASM == backend)) {
		fprintf(stderr,
			"error: legacy mode needs --backend gcc or interp\n");
		return 1;
	}
	if (JIT_INTERP != backend && !(legacy && JIT_GCC == backend)) {
		fprintf(stderr, "error: %s backend arrives in a later version\n",
			JIT_GCC == backend ? "gcc" :
			JIT_X86 == backend ? "x86" : "wasm");
		return 1;
	}
	if (JIT_GCC == backend) {
		return run_legacy_gcc(e);
	}
	if (legacy) {
		printf("%f\n", jit_interp(e, vals));
	}
	else {
		ir_fmt_double(buf, sizeof (buf), jit_interp(e, vals));
		printf("%s\n", buf);
	}
	return 0;
}

int
main(int argc, char *argv[])
{
	jit_expr *e = NULL;
	int status = run(argc, argv, &e);

	jit_free(e);
	return status;
}
