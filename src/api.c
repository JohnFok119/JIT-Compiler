/* Implementation of the public API in include/jit.h. */

#include <stdlib.h>
#include <string.h>
#include "api.h"
#include "front/parser.h"

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
