/* JS-facing API of the Emscripten build (make wasm -> web/dist/engine.mjs).
 *
 * One handle = one parsed expression: the source as written (tokens, ast,
 * ir), the optimized one (ir_opt, asm, hex, wasm, interp), the display form
 * (infix) and its derivative with respect to the first variable. The x86
 * backend still encodes here so the asm / hex stages work, but nothing in
 * this file ever runs that code. */

#include <emscripten.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api.h"
#include "backend/wasm/emit.h"
#include "jit.h"

#define MAX_HANDLES 64

struct handle {
	jit_expr *raw;          /* as parsed */
	jit_expr *opt;          /* optimized at the load level */
	jit_expr *disp;         /* display form, for the infix stage */
	jit_expr *diff;         /* d/d(first variable), optimized at the load level */
	jit_expr *diff_disp;    /* the same derivative in display form, for the diff stage */
	double vars[JIT_MAX_VARS];
	unsigned char *wasm[2]; /* lazily emitted: 0 = f, 1 = f' */
	size_t wasm_len[2];
};

static struct handle handles[MAX_HANDLES];
static char err_json[256] = "{\"pos\":-1,\"msg\":\"\"}";
static char *text;          /* result of the last jitw_stage call */

/* Writes {"pos":N,"msg":"..."} with the message JSON-escaped. */
static void
set_error(int pos, const char *msg)
{
	char esc[160];
	size_t n = 0;

	for (; *msg && n + 7 < sizeof (esc); ++msg) {
		unsigned char c = (unsigned char)*msg;

		if ('"' == c || '\\' == c) {
			esc[n++] = '\\';
			esc[n++] = (char)c;
		}
		else if (c < 0x20) {
			esc[n++] = ' ';
		}
		else {
			esc[n++] = (char)c;
		}
	}
	esc[n] = '\0';
	snprintf(err_json, sizeof (err_json), "{\"pos\":%d,\"msg\":\"%s\"}", pos, esc);
}

static void
release(struct handle *h)
{
	jit_free(h->raw);
	jit_free(h->opt);
	jit_free(h->disp);
	jit_free(h->diff);
	jit_free(h->diff_disp);
	free(h->wasm[0]);
	free(h->wasm[1]);
	memset(h, 0, sizeof (*h));
}

static struct handle *
lookup(int h)
{
	if (h < 1 || h > MAX_HANDLES || !handles[h - 1].raw) {
		return NULL;
	}
	return &handles[h - 1];
}

EMSCRIPTEN_KEEPALIVE int
jitw_load(const char *src, int opt_level)
{
	struct jit_error err;
	struct handle *h = NULL;
	const char *var;
	int i;

	if (0 != opt_level && 2 != opt_level) {
		set_error(-1, "bad optimization level (use 0 or 2)");
		return 0;
	}
	for (i = 0; i < MAX_HANDLES && !h; ++i) {
		if (!handles[i].raw) {
			h = &handles[i];
		}
	}
	if (!h) {
		set_error(-1, "too many expressions loaded");
		return 0;
	}
	if (!(h->raw = jit_parse(src, 0, &err)) ||
	    !(h->opt = jit_parse(src, 0, &err)) ||
	    !(h->disp = jit_parse(src, 0, &err))) {
		set_error(err.pos, err.msg);
		release(h);
		return 0;
	}
	var = jit_nvars(h->raw) ? jit_var_name(h->raw, 0) : "";
	if (!(h->diff = jit_diff(h->raw, var, &err)) ||
	    !(h->diff_disp = jit_diff(h->raw, var, &err))) {
		set_error(err.pos, err.msg);
		release(h);
		return 0;
	}
	if (opt_level && (jit_optimize(h->opt, opt_level) ||
			  jit_display_form(h->disp) ||
			  jit_optimize(h->diff, opt_level))) {
		set_error(-1, "out of memory");
		release(h);
		return 0;
	}
	return (int)(h - handles) + 1;
}

EMSCRIPTEN_KEEPALIVE const char *
jitw_error_json(void)
{
	return err_json;
}

/* The text of one stage, kept in the static `text`; NULL (error set) on failure. */
static const char *
stage_text(const struct handle *h, const char *stage)
{
	struct jit_error err;
	size_t size = 0;
	FILE *f;
	int rc = 0;

	free(text);
	text = NULL;
	if (!(f = open_memstream(&text, &size))) {
		set_error(-1, "out of memory");
		return NULL;
	}
	if (!strcmp(stage, "tokens") || !strcmp(stage, "ast") ||
	    !strcmp(stage, "ir")) {
		rc = jit_dump(h->raw, stage, f);
	}
	else if (!strcmp(stage, "ir_opt")) {
		rc = jit_dump(h->opt, "ir", f);
	}
	else if (!strcmp(stage, "infix")) {
		rc = jit_dump(h->disp, "infix", f);
	}
	else if (!strcmp(stage, "diff")) {
		rc = jit_dump(h->diff_disp, "infix", f);
	}
	else if (!strcmp(stage, "diff_ir")) {
		rc = jit_dump(h->diff, "ir", f);
	}
	else if (!strcmp(stage, "asm") || !strcmp(stage, "hex") ||
		 !strcmp(stage, "wasm")) {
		jit_fn *fn = jit_compile(h->opt, strcmp(stage, "wasm") ?
					 JIT_X86 : JIT_WASM, &err);

		if (!fn) {
			fclose(f);
			free(text);
			text = NULL;
			set_error(err.pos, err.msg);
			return NULL;
		}
		rc = jit_fn_dump(fn, stage, f);
		jit_fn_free(fn);
	}
	else {
		rc = -1;
	}
	if (fclose(f) || rc) {
		free(text);
		text = NULL;
		set_error(-1, rc ? "unknown stage" : "out of memory");
		return NULL;
	}
	return text ? text : "";
}

EMSCRIPTEN_KEEPALIVE const char *
jitw_stage(int h, const char *stage)
{
	struct handle *p = lookup(h);

	if (!p) {
		set_error(-1, "bad handle");
		return NULL;
	}
	return stage_text(p, stage);
}

EMSCRIPTEN_KEEPALIVE int
jitw_nvars(int h)
{
	struct handle *p = lookup(h);

	return p ? jit_nvars(p->raw) : 0;
}

EMSCRIPTEN_KEEPALIVE const char *
jitw_var_name(int h, int i)
{
	struct handle *p = lookup(h);

	return p ? jit_var_name(p->raw, i) : NULL;
}

/* The module bytes for which (0 = f, 1 = f'), emitted on first use; NULL on error. */
static struct handle *
ensure_wasm(int h, int which)
{
	struct handle *p = lookup(h);
	struct jit_error err;

	if (!p || (0 != which && 1 != which)) {
		return NULL;
	}
	if (!p->wasm[which] &&
	    wasm_emit(&(which ? p->diff : p->opt)->ir, &p->wasm[which],
		      &p->wasm_len[which], &err)) {
		set_error(err.pos, err.msg);
		return NULL;
	}
	return p;
}

/* The bytes belong to the handle (valid until jitw_free). */
EMSCRIPTEN_KEEPALIVE int
jitw_wasm(int h, int which)
{
	struct handle *p = ensure_wasm(h, which);

	return p ? (int)(uintptr_t)p->wasm[which] : 0;
}

EMSCRIPTEN_KEEPALIVE int
jitw_wasm_len(int h, int which)
{
	struct handle *p = ensure_wasm(h, which);

	return p ? (int)p->wasm_len[which] : 0;
}

EMSCRIPTEN_KEEPALIVE void
jitw_set_var(int h, int i, double v)
{
	struct handle *p = lookup(h);

	if (p && 0 <= i && i < JIT_MAX_VARS) {
		p->vars[i] = v;
	}
}

EMSCRIPTEN_KEEPALIVE double
jitw_interp(int h, int which)
{
	struct handle *p = lookup(h);

	if (!p) {
		return 0.0;
	}
	return jit_interp(which ? p->diff : p->opt, p->vars);
}

/* Sum of the interpreter over n values of variable 0 (the others keep their
 * jitw_set_var values): one call instead of 2n, same evaluation path as
 * jitw_interp. NaN propagates. */
EMSCRIPTEN_KEEPALIVE double
jitw_interp_sum(int h, int which, const double *xs, int n)
{
	struct handle *p = lookup(h);
	double sum = 0.0;
	int i;

	if (!p) {
		return 0.0;
	}
	for (i = 0; i < n; ++i) {
		p->vars[0] = xs[i];
		sum += jit_interp(which ? p->diff : p->opt, p->vars);
	}
	return sum;
}

EMSCRIPTEN_KEEPALIVE void
jitw_free(int h)
{
	struct handle *p = lookup(h);

	if (p) {
		release(p);
	}
}
