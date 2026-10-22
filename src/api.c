/* Implementation of the public API in include/jit.h. */

#include <stdlib.h>
#include <string.h>
#include "api.h"
#include "backend/gcc/loader.h"
#include "backend/x86_64/emit.h"
#include "front/parser.h"

struct jit_fn {
	enum jit_backend backend;
	struct ir ir;             /* JIT_INTERP: snapshot of the nodes */
	struct x86_code x86;      /* JIT_X86 */
	struct gcc_code gcc;      /* JIT_GCC */
};

static jit_expr *
oom(jit_expr *e, struct jit_error *err)
{
	jit_free(e);
	err->pos = -1;
	snprintf(err->msg, sizeof (err->msg), "out of memory");
	return NULL;
}

jit_expr *
jit_parse(const char *src, int legacy, struct jit_error *err)
{
	struct parse_result res;
	jit_expr *e;
	size_t n = strlen(src);

	if (!(e = calloc(1, sizeof (*e)))) {
		return oom(NULL, err);
	}
	ir_init(&e->ir);
	if (!(e->src = malloc(n + 1))) {
		return oom(e, err);
	}
	memcpy(e->src, src, n + 1);
	if (lex(e->src, &e->toks)) {
		*err = e->toks.err;
		jit_free(e);
		return NULL;
	}
	if (parse(e->src, &e->toks, legacy, &res, err)) {
		jit_free(e);
		return NULL;
	}
	e->ast = res.root;
	e->ir.legacy = legacy ? 1 : 0;
	e->ir.nvars = res.nvars;
	memcpy(e->ir.vars, res.vars, sizeof (e->ir.vars));
	if (ir_lower(&e->ir, e->ast)) {
		return oom(e, err);
	}
	return e;
}

void
jit_free(jit_expr *e)
{
	if (e) {
		ir_free(&e->ir);
		ast_free(e->ast);
		token_list_free(&e->toks);
		free(e->src);
		free(e);
	}
}

int
jit_nvars(const jit_expr *e)
{
	return e->ir.nvars;
}

const char *
jit_var_name(const jit_expr *e, int i)
{
	if (0 > i || i >= e->ir.nvars) {
		return NULL;
	}
	return e->ir.vars[i];
}

double
jit_interp(const jit_expr *e, const double *vars)
{
	return ir_interp(&e->ir, vars);
}

int
jit_dump(const jit_expr *e, const char *stage, FILE *out)
{
	if (!strcmp(stage, "tokens")) {
		token_list_print(&e->toks, e->src, out);
	}
	else if (!strcmp(stage, "ast")) {
		ast_print(e->ast, (const char (*)[32])e->ir.vars, out);
	}
	else if (!strcmp(stage, "ir")) {
		ir_print(&e->ir, out);
	}
	else {
		return -1;
	}
	return 0;
}

static jit_fn *
compile_error(jit_fn *fn, struct jit_error *err, const char *msg)
{
	jit_fn_free(fn);
	err->pos = -1;
	snprintf(err->msg, sizeof (err->msg), "%s", msg);
	return NULL;
}

jit_fn *
jit_compile(const jit_expr *e, enum jit_backend backend, struct jit_error *err)
{
	jit_fn *fn;

	if (JIT_WASM == backend) {
		return compile_error(NULL, err,
				     "wasm backend arrives in a later version");
	}
	if (JIT_INTERP != backend && JIT_X86 != backend && JIT_GCC != backend) {
		return compile_error(NULL, err, "unknown backend");
	}
	if (!(fn = calloc(1, sizeof (*fn)))) {
		return compile_error(NULL, err, "out of memory");
	}
	fn->backend = backend;
	ir_init(&fn->ir);
	if (JIT_X86 == backend) {
		if (x86_compile(&e->ir, &fn->x86, err)) {
			jit_fn_free(fn);
			return NULL;
		}
		return fn;
	}
	if (JIT_GCC == backend) {
		if (gcc_compile(&e->ir, &fn->gcc, err)) {
			jit_fn_free(fn);
			return NULL;
		}
		return fn;
	}
	/* JIT_INTERP: copy the nodes the interpreter reads */
	fn->ir.count = fn->ir.cap = e->ir.count;
	fn->ir.root = e->ir.root;
	fn->ir.nvars = e->ir.nvars;
	fn->ir.legacy = e->ir.legacy;
	memcpy(fn->ir.vars, e->ir.vars, sizeof (fn->ir.vars));
	if (e->ir.count &&
	    !(fn->ir.nodes = malloc((size_t)e->ir.count * sizeof (*fn->ir.nodes)))) {
		return compile_error(fn, err, "out of memory");
	}
	if (e->ir.count) {
		memcpy(fn->ir.nodes, e->ir.nodes,
		       (size_t)e->ir.count * sizeof (*fn->ir.nodes));
	}
	return fn;
}

jit_native
jit_entry(const jit_fn *fn)
{
	jit_native f = NULL;

	if (JIT_X86 == fn->backend) {
		/* object pointer -> function pointer without a pedantic cast */
		memcpy(&f, &fn->x86.mem, sizeof (f));
	}
	else if (JIT_GCC == fn->backend) {
		/* (a legacy module's evaluate takes no argument: the pointer is
		   only good for jit_call) */
		memcpy(&f, &fn->gcc.fn, sizeof (f));
	}
	return f;
}

double
jit_call(const jit_fn *fn, const double *vars)
{
	if (JIT_X86 == fn->backend) {
		return jit_entry(fn)(vars);
	}
	if (JIT_GCC == fn->backend) {
		return gcc_call(&fn->gcc, vars);
	}
	return ir_interp(&fn->ir, vars);
}

void
jit_fn_free(jit_fn *fn)
{
	if (fn) {
		ir_free(&fn->ir);
		x86_code_free(&fn->x86);
		gcc_code_free(&fn->gcc);
		free(fn);
	}
}

int
jit_fn_dump(const jit_fn *fn, const char *stage, FILE *out)
{
	if (JIT_X86 == fn->backend) {
		return x86_dump(&fn->x86, stage, out);
	}
	if (JIT_GCC == fn->backend) {
		return gcc_dump(&fn->gcc, stage, out);
	}
	return -1;
}

size_t
jit_fn_code_size(const jit_fn *fn)
{
	return JIT_X86 == fn->backend ? fn->x86.size : 0;
}
