/* Command-line front end: jit [options] '<expr>' [name=value ...] */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "jit.h"
#include "ir/ir.h"
#include "cli/bench.h"

#define USAGE \
	"usage: jit [--legacy] [-O0|-O2] [--emit tokens|ast|ir|infix|asm|hex|c] " \
	"[--diff VAR] [--backend interp|x86|gcc] [--bench [N]] [--help] [--version] " \
	"'<expr>' [name=value ...]"

#define MAX_LINE 4096

struct opts {
	const char *emit;       /* NULL, or the stage to print */
	int legacy;
	int backend;            /* enum jit_backend, or -1 = default */
	int opt;                /* 0 or 2 */
	const char *diff;       /* NULL, or the variable to differentiate by */
	long bench;             /* 0, or the evaluations per run for --bench */
};

static int use_color;       /* red "error:" and dim caret, only on a TTY */

static int
usage(void)
{
	fprintf(stderr, USAGE "\n");
	return 2;
}

static int
help(void)
{
	printf(USAGE "\n\n"
	       "With no expression on a terminal, jit starts an interactive prompt.\n"
	       "\n"
	       "options (one example each):\n"
	       "  --backend interp|x86|gcc   how the expression runs (default x86; gcc with --legacy)\n"
	       "      jit --backend gcc 'sin(x) * 2' x=0.5\n"
	       "  -O0 | -O2                  optimizer level (default -O2)\n"
	       "      jit -O0 '2 * 3 + x' x=1\n"
	       "  --emit STAGE               print a stage instead of running; STAGE is\n"
	       "                             tokens, ast, ir, infix, asm, hex (x86 backend) or c (gcc backend)\n"
	       "      jit --emit ir 'x * 1 + 0'\n"
	       "  --diff VAR                 differentiate by VAR: prints the derivative as an expression\n"
	       "                             (or its value, given name=value); kinks show as step(...),\n"
	       "                             which is printed but not parseable\n"
	       "      jit --diff x 'x^3 + sin(x)*x'\n"
	       "      jit --diff x 'sigmoid(3*x)' x=0.2\n"
	       "      jit --backend gcc --emit c 'sigmoid(x) * 2'\n"
	       "  --bench [N]                time compile + N evaluations (default 1000000) on every\n"
	       "                             backend: interp, x86 -O0, x86 -O2, gcc\n"
	       "      jit --bench 100000 'sin(x) * 2'\n"
	       "  --legacy                   the original assignment: sigmoid of the result, x/0 = 0\n"
	       "      jit --legacy '10 / 4'\n"
	       "  --help                     show this text\n"
	       "      jit --help\n"
	       "  --version                  show the version\n"
	       "      jit --version\n"
	       "\n"
	       "prompt commands: :q quits, :emit STAGE turns a stage dump on or off\n");
	return 0;
}

static int
is_letter(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* "error: ..." line on stderr, "error:" in red on a colour terminal. */
static void
errorf(const char *fmt, ...)
{
	va_list ap;

	fputs(use_color ? "\033[31merror:\033[0m " : "error: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static int
print_parse_error(const char *src, const struct jit_error *err)
{
	int i;

	errorf("%s", err->msg);
	if (0 <= err->pos) {
		fprintf(stderr, "  %s\n", src);
		if (use_color) {
			fputs("\033[2m", stderr);
		}
		fputs("  ", stderr);
		for (i = 0; i < err->pos && src[i]; ++i) {
			fputc('\t' == src[i] ? '\t' : ' ', stderr);
		}
		fputc('^', stderr);
		if (use_color) {
			fputs("\033[0m", stderr);
		}
		fputc('\n', stderr);
	}
	return 1;
}

/*
 * Evaluates one expression with the given options. bind[0..nbind) are the
 * name=value strings. repl != 0: keep going after a stage dump and print
 * the result as well. Returns the process exit status (0 ok, 1 error).
 */
static int
evaluate_expr(const struct opts *o, const char *src, const char **bind,
	      int nbind, int repl)
{
	const char *emit = o->emit;
	int legacy = o->legacy, backend = o->backend, opt = o->opt;
	int j, k, code_emit, status = 1;
	double vals[JIT_MAX_VARS] = { 0 };
	int have[JIT_MAX_VARS] = { 0 };
	struct jit_error err;
	jit_fn *fn = NULL;
	char buf[32];
	jit_expr *e;

	if (!(e = jit_parse(src, legacy, &err))) {
		return print_parse_error(src, &err);
	}

	/* name=value arguments; a repeated name: the last value wins */
	for (j = 0; j < nbind; ++j) {
		const char *eq = strchr(bind[j], '=');
		int len = (int)(eq - bind[j]);
		char *end;
		double v;

		for (k = 0; k < jit_nvars(e); ++k) {
			const char *name = jit_var_name(e, k);
			if ((int)strlen(name) == len &&
			    !memcmp(name, bind[j], (size_t)len)) {
				break;
			}
		}
		if (k == jit_nvars(e)) {
			errorf("'%.*s' is not used in the expression", len, bind[j]);
			goto done;
		}
		v = strtod(eq + 1, &end);
		if (!eq[1] || *end) {
			errorf("bad value for '%.*s'", len, bind[j]);
			goto done;
		}
		vals[k] = v;
		have[k] = 1;
	}

	if (legacy) {
		opt = 0;        /* legacy is never optimized */
	}
	if (o->diff) {
		jit_expr *d;

		/* tokens and ast describe the input; everything else, the derivative */
		if (emit && (!strcmp(emit, "tokens") || !strcmp(emit, "ast"))) {
			jit_dump(e, emit, stdout);
			if (!repl) {
				status = 0;
				goto done;
			}
			emit = NULL;
		}
		if (!(d = jit_diff(e, o->diff, &err))) {
			errorf("%s", err.msg);
			goto done;
		}
		jit_free(e);
		e = d;
		if (!emit && !nbind) {
			emit = "infix";
		}
	}
	/* asm / hex / c dump the compiled code, so they go through jit_compile */
	code_emit = emit && (!strcmp(emit, "asm") || !strcmp(emit, "hex") ||
			     !strcmp(emit, "c"));
	if (emit && !code_emit) {
		if (!strcmp(emit, "infix") && opt && jit_display_form(e)) {
			errorf("out of memory");
			goto done;
		}
		jit_dump(e, emit, stdout);
		if (opt && !o->diff && !strcmp(emit, "ir")) {
			if (jit_optimize(e, opt)) {
				errorf("out of memory");
				goto done;
			}
			printf("; after -O%d\n", opt);
			jit_dump(e, emit, stdout);
		}
		if (!repl) {
			status = 0;
			goto done;
		}
	}

	for (k = 0; k < jit_nvars(e) && (!code_emit || repl); ++k) {
		if (!have[k]) {
			errorf("no value for variable '%s'", jit_var_name(e, k));
			goto done;
		}
	}

	if (opt && jit_optimize(e, opt)) {
		errorf("out of memory");
		goto done;
	}

	if (0 > backend) {
		backend = legacy ? JIT_GCC : JIT_X86;
	}
	if (legacy && (JIT_X86 == backend || JIT_WASM == backend)) {
		errorf("legacy mode needs --backend gcc or interp");
		goto done;
	}
	if (!(fn = jit_compile(e, (enum jit_backend)backend, &err))) {
		errorf("%s", err.msg);
		goto done;
	}
	if (code_emit) {
		if (jit_fn_dump(fn, emit, stdout)) {
			errorf("--emit %s needs --backend %s", emit,
			       strcmp(emit, "c") ? "x86" : "gcc");
			goto done;
		}
		if (!repl) {
			status = 0;
			goto done;
		}
	}
	{
		double result = jit_call(fn, vals);

		if (legacy) {
			printf("%f\n", result);
		}
		else {
			ir_fmt_double(buf, sizeof (buf), result);
			printf("%s\n", buf);
		}
	}
	status = 0;
done:
	jit_fn_free(fn);
	jit_free(e);
	return status;
}

/* Splits a REPL line into the expression and trailing name=value tokens
   (the expression itself can never contain '='). Modifies line. */
static const char *
split_line(char *line, const char **bind, int *nbind)
{
	char *eq = strchr(line, '='), *p, *tok;

	*nbind = 0;
	if (!eq) {
		return line;
	}
	/* back up to the start of the name before the first '=' */
	p = eq;
	while (p > line && (isalnum((unsigned char)p[-1]) || '_' == p[-1])) {
		--p;
	}
	if (p > line && ' ' != p[-1] && '\t' != p[-1]) {
		return line;    /* not name=value: let the parser report it */
	}
	for (tok = strtok(p, " \t"); tok && *nbind < 64; tok = strtok(NULL, " \t")) {
		bind[(*nbind)++] = tok;
	}
	if (p == line) {
		return "";      /* only name=value tokens: empty expression */
	}
	p[-1] = '\0';           /* end the expression at the blank before them */
	return line;
}

/* Interactive loop: plain fgets, no readline. */
static int
repl(struct opts o)
{
	char line[MAX_LINE];
	char stage[16];

	stage[0] = '\0';
	if (o.emit) {
		snprintf(stage, sizeof (stage), "%s", o.emit);
	}
	for (;;) {
		const char *bind[64];
		const char *expr;
		struct opts cur = o;
		size_t n;
		int nbind, nonblank = 0;
		char *p;

		fputs("jit> ", stdout);
		fflush(stdout);
		if (!fgets(line, sizeof (line), stdin)) {
			fputc('\n', stdout);
			return 0;
		}
		n = strlen(line);
		if (n && '\n' == line[n - 1]) {
			line[--n] = '\0';
		}
		else if (n == sizeof (line) - 1) {
			int c;

			while ((c = getchar()) != EOF && '\n' != c) {
			}
			errorf("line too long");
			continue;
		}
		for (p = line; *p; ++p) {
			nonblank |= ' ' != *p && '\t' != *p && '\r' != *p;
		}
		if (!nonblank) {
			continue;
		}
		for (p = line; ' ' == *p || '\t' == *p; ++p) {
		}
		if (!strcmp(p, ":q")) {
			return 0;
		}
		if (!strncmp(p, ":emit", 5) && (!p[5] || ' ' == p[5] || '\t' == p[5])) {
			char want[16] = "";

			sscanf(p + 5, "%15s", want);
			if (strcmp(want, "tokens") && strcmp(want, "ast") &&
			    strcmp(want, "ir") && strcmp(want, "infix") &&
			    strcmp(want, "asm") &&
			    strcmp(want, "hex") && strcmp(want, "c")) {
				errorf("usage: :emit tokens|ast|ir|infix|asm|hex|c");
			}
			else if (!strcmp(stage, want)) {
				stage[0] = '\0';
				printf("emit off\n");
			}
			else {
				snprintf(stage, sizeof (stage), "%s", want);
				printf("emit %s\n", stage);
			}
			continue;
		}
		if (':' == *p) {
			errorf("unknown command (try :q or :emit STAGE)");
			continue;
		}
		cur.emit = stage[0] ? stage : NULL;
		expr = split_line(p, bind, &nbind);
		evaluate_expr(&cur, expr, bind, nbind, 1);
	}
}

int
main(int argc, char *argv[])
{
	struct opts o = { NULL, 0, -1, 2, NULL, 0 };
	const char *src, **bind;
	int i = 1, j;

	use_color = isatty(2) && !getenv("NO_COLOR");

	/* options: exact strings, only before the expression */
	while (i < argc) {
		const char *a = argv[i];

		if (!strcmp(a, "--legacy")) {
			o.legacy = 1;
			++i;
		}
		else if (!strcmp(a, "--help")) {
			return help();
		}
		else if (!strcmp(a, "--version")) {
			printf("jit 2.0\n");
			return 0;
		}
		else if (!strcmp(a, "-O0") || !strcmp(a, "-O2")) {
			o.opt = 'O' == a[1] && '0' == a[2] ? 0 : 2;
			++i;
		}
		else if (!strcmp(a, "--emit")) {
			if (i + 1 >= argc ||
			    (strcmp(argv[i + 1], "tokens") &&
			     strcmp(argv[i + 1], "ast") &&
			     strcmp(argv[i + 1], "ir") &&
			     strcmp(argv[i + 1], "infix") &&
			     strcmp(argv[i + 1], "asm") &&
			     strcmp(argv[i + 1], "hex") &&
			     strcmp(argv[i + 1], "c"))) {
				return usage();
			}
			o.emit = argv[i + 1];
			i += 2;
		}
		else if (!strcmp(a, "--bench")) {
			o.bench = 1000000;
			++i;
			if (i + 1 < argc && isdigit((unsigned char)argv[i][0])) {
				char *end;

				o.bench = strtol(argv[i], &end, 10);
				if (*end || o.bench < 1) {
					return usage();
				}
				++i;
			}
		}
		else if (!strcmp(a, "--diff")) {
			if (i + 1 >= argc || !argv[i + 1][0]) {
				return usage();
			}
			o.diff = argv[i + 1];
			i += 2;
		}
		else if (!strcmp(a, "--backend")) {
			const char *v = i + 1 < argc ? argv[i + 1] : "";

			if (!strcmp(v, "interp")) {
				o.backend = JIT_INTERP;
			}
			else if (!strcmp(v, "gcc")) {
				o.backend = JIT_GCC;
			}
			else if (!strcmp(v, "x86")) {
				o.backend = JIT_X86;
			}
			else if (!strcmp(v, "wasm")) {
				o.backend = JIT_WASM;
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
		/* no expression: a prompt on a terminal, usage otherwise */
		return isatty(0) ? repl(o) : usage();
	}
	src = argv[i++];
	if (o.bench) {
		if (i < argc || o.legacy || o.diff || o.emit) {
			return usage();
		}
		return bench_run(src, o.bench);
	}
	for (j = i; j < argc; ++j) {
		const char *eq = strchr(argv[j], '=');
		if (!eq || eq == argv[j]) {
			return usage();
		}
	}
	bind = (const char **)&argv[i];
	return evaluate_expr(&o, src, bind, argc - i, 0);
}
