/* Originally based on the UC Irvine CS 238P skeleton by Tony Givargis. */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "front/parser.h"

#define LEGACY_MSG "legacy mode supports numbers and + - * / only"
#define MAX_DEPTH 256   /* nested groups, calls, unary minus and ^ chains */

/*
 * Grammar (core.md):
 *   expr    := add
 *   add     := mul (('+' | '-') mul)*
 *   mul     := unary (('*' | '/') unary)*
 *   unary   := ('+' | '-') unary | power
 *   power   := primary ('^' unary)?
 *   primary := NUMBER | NAME | NAME '(' args ')' | '(' expr ')'
 *   args    := expr (',' expr)*
 */

struct parser {
	const char *src;
	const struct token_list *toks;
	int i;
	int legacy;
	int failed;
	int depth;              /* current nesting, see MAX_DEPTH */
	struct parse_result *res;
	struct jit_error *err;
};

static void
fail(struct parser *p, int pos, const char *fmt, ...)
{
	va_list ap;

	if (p->failed) {
		return; /* the first error wins */
	}
	p->failed = 1;
	p->err->pos = pos;
	va_start(ap, fmt);
	vsnprintf(p->err->msg, sizeof (p->err->msg), fmt, ap);
	va_end(ap);
}

static void *
oom(struct parser *p)
{
	fail(p, -1, "out of memory");
	return NULL;
}

/* Enters one nesting level (group, call, unary chain, ^ chain); 0 and an
   error when the cap is exceeded. Every success is paired with --p->depth. */
static int
enter(struct parser *p, int pos)
{
	if (p->depth >= MAX_DEPTH) {
		fail(p, pos, "expression nests too deeply");
		return 0;
	}
	++p->depth;
	return 1;
}

/* Current token; reaching a TOK_ERROR reports the lexical error. */
static const struct token *
peek(struct parser *p)
{
	const struct token *t = &p->toks->v[p->i];

	if (TOK_ERROR == t->kind && !p->failed) {
		p->failed = 1;
		*p->err = p->toks->err;
	}
	return t;
}

static void
advance(struct parser *p)
{
	enum tok_kind k = p->toks->v[p->i].kind;

	if (TOK_END != k && TOK_ERROR != k) {
		++p->i;
	}
}

static int
is_op(const struct token *t, char op)
{
	return TOK_OP == t->kind && op == t->op;
}

static struct ast *
node(struct parser *p, enum ast_kind kind, int pos, struct ast *a,
     struct ast *b)
{
	struct ast *n;

	if (!(n = ast_new(kind, pos))) {
		ast_free(a);
		ast_free(b);
		return oom(p);
	}
	n->a = a;
	n->b = b;
	return n;
}

static struct ast *expr(struct parser *p);
static struct ast *unary(struct parser *p);

static struct ast *
call(struct parser *p, const struct token *name)
{
	const char *s = p->src + name->pos;
	struct ast *args[2] = { NULL, NULL };
	struct ast *arg, *n;
	int fn, count = 0, arity;

	if (0 > (fn = ast_find_func(s, name->len))) {
		if (0 <= ast_find_const(s, name->len)) {
			fail(p, name->pos, "'%.*s' is a constant",
			     name->len, s);
		}
		else {
			fail(p, name->pos, "unknown function '%.*s'",
			     name->len, s);
		}
		return NULL;
	}
	arity = ast_funcs[fn].arity;
	advance(p); /* '(' */
	if (TOK_RPAREN != peek(p)->kind) {
		for (;;) {
			if (!(arg = expr(p))) {
				goto error;
			}
			if (count < 2) {
				args[count] = arg;
			}
			else {
				ast_free(arg);
			}
			++count;
			if (TOK_COMMA != peek(p)->kind) {
				break;
			}
			advance(p);
		}
	}
	if (TOK_RPAREN != peek(p)->kind) {
		fail(p, peek(p)->pos, "expected ')'");
		goto error;
	}
	advance(p);
	if (count != arity) {
		fail(p, name->pos, "'%s' takes %d argument%s",
		     ast_funcs[fn].name, arity, 1 == arity ? "" : "s");
		goto error;
	}
	if (!(n = node(p, AST_CALL, name->pos, args[0], args[1]))) {
		return NULL;
	}
	n->fn = fn;
	return n;
error:
	ast_free(args[0]);
	ast_free(args[1]);
	return NULL;
}

static struct ast *
variable(struct parser *p, const struct token *name)
{
	const char *s = p->src + name->pos;
	struct parse_result *r = p->res;
	struct ast *n;
	int i;

	for (i = 0; i < r->nvars; ++i) {
		if ((int)strlen(r->vars[i]) == name->len &&
		    !memcmp(r->vars[i], s, (size_t)name->len)) {
			break;
		}
	}
	if (i == r->nvars) {
		if (JIT_MAX_VARS == r->nvars) {
			fail(p, name->pos, "too many variables (max %d)",
			     JIT_MAX_VARS);
			return NULL;
		}
		memcpy(r->vars[i], s, (size_t)name->len);
		r->vars[i][name->len] = '\0';
		++r->nvars;
	}
	if (!(n = node(p, AST_VAR, name->pos, NULL, NULL))) {
		return NULL;
	}
	n->var = i;
	return n;
}

static struct ast *
name_primary(struct parser *p)
{
	const struct token *name = peek(p);
	const char *s = p->src + name->pos;
	struct ast *n;
	int k;

	if (p->legacy) {
		fail(p, name->pos, LEGACY_MSG);
		return NULL;
	}
	advance(p);
	if (TOK_LPAREN == peek(p)->kind) {
		if (!enter(p, peek(p)->pos)) {
			return NULL;
		}
		n = call(p, name);
		--p->depth;
		return n;
	}
	if (p->failed) {
		return NULL;
	}
	if (0 <= ast_find_func(s, name->len)) {
		fail(p, name->pos, "'%.*s' is a function", name->len, s);
		return NULL;
	}
	if (0 <= (k = ast_find_const(s, name->len))) {
		if (!(n = node(p, AST_CONST, name->pos, NULL, NULL))) {
			return NULL;
		}
		n->fn = k;
		n->num = ast_consts[k].value;
		return n;
	}
	return variable(p, name);
}

static struct ast *
primary(struct parser *p)
{
	const struct token *t = peek(p);
	struct ast *n;

	if (TOK_NUMBER == t->kind) {
		if (!(n = node(p, AST_NUM, t->pos, NULL, NULL))) {
			return NULL;
		}
		n->num = t->num;
		advance(p);
		return n;
	}
	if (TOK_NAME == t->kind) {
		return name_primary(p);
	}
	if (TOK_LPAREN == t->kind) {
		if (!enter(p, t->pos)) {
			return NULL;
		}
		advance(p);
		n = expr(p);
		--p->depth;
		if (!n) {
			return NULL;
		}
		if (TOK_RPAREN != peek(p)->kind) {
			fail(p, peek(p)->pos, "expected ')'");
			ast_free(n);
			return NULL;
		}
		advance(p);
		return n;
	}
	fail(p, t->pos, "expected a number, name or '('");
	return NULL;
}

static struct ast *
power(struct parser *p)
{
	const struct token *t;
	struct ast *base, *exponent;
	int pos;

	if (!(base = primary(p))) {
		return NULL;
	}
	t = peek(p);
	if (!is_op(t, '^')) {
		return base;
	}
	pos = t->pos;
	if (p->legacy) {
		fail(p, pos, LEGACY_MSG);
		ast_free(base);
		return NULL;
	}
	if (!enter(p, pos)) {
		ast_free(base);
		return NULL;
	}
	advance(p);
	exponent = unary(p);
	--p->depth;
	if (!exponent) {
		ast_free(base);
		return NULL;
	}
	return node(p, AST_POW, pos, base, exponent);
}

static struct ast *
unary(struct parser *p)
{
	const struct token *t = peek(p);
	struct ast *a;
	int pos = t->pos;

	if (is_op(t, '+')) {
		if (!enter(p, pos)) {
			return NULL;
		}
		advance(p);
		a = unary(p);
		--p->depth;
		return a;
	}
	if (is_op(t, '-')) {
		if (!enter(p, pos)) {
			return NULL;
		}
		advance(p);
		a = unary(p);
		--p->depth;
		if (!a) {
			return NULL;
		}
		return node(p, AST_NEG, pos, a, NULL);
	}
	return power(p);
}

static struct ast *
mul(struct parser *p)
{
	struct ast *left, *right;

	if (!(left = unary(p))) {
		return NULL;
	}
	for (;;) {
		const struct token *t = peek(p);
		enum ast_kind k;
		int pos = t->pos;

		if (is_op(t, '*')) {
			k = AST_MUL;
		}
		else if (is_op(t, '/')) {
			k = AST_DIV;
		}
		else {
			return left;
		}
		advance(p);
		if (!(right = unary(p))) {
			ast_free(left);
			return NULL;
		}
		if (!(left = node(p, k, pos, left, right))) {
			return NULL;
		}
	}
}

static struct ast *
add(struct parser *p)
{
	struct ast *left, *right;

	if (!(left = mul(p))) {
		return NULL;
	}
	for (;;) {
		const struct token *t = peek(p);
		enum ast_kind k;
		int pos = t->pos;

		if (is_op(t, '+')) {
			k = AST_ADD;
		}
		else if (is_op(t, '-')) {
			k = AST_SUB;
		}
		else {
			return left;
		}
		advance(p);
		if (!(right = mul(p))) {
			ast_free(left);
			return NULL;
		}
		if (!(left = node(p, k, pos, left, right))) {
			return NULL;
		}
	}
}

static struct ast *
expr(struct parser *p)
{
	return add(p);
}

int
parse(const char *src, const struct token_list *toks, int legacy,
      struct parse_result *out, struct jit_error *err)
{
	struct parser p;
	const struct token *t;
	struct ast *root;

	memset(out, 0, sizeof (*out));
	memset(&p, 0, sizeof (p));
	p.src = src;
	p.toks = toks;
	p.legacy = legacy;
	p.res = out;
	p.err = err;
	if (TOK_END == toks->v[0].kind) {
		fail(&p, toks->v[0].pos, "empty expression");
		return -1;
	}
	root = expr(&p);
	if (root && TOK_END != (t = peek(&p))->kind) {
		fail(&p, t->pos, "unexpected '%.*s' after expression",
		     t->len < 64 ? t->len : 64, src + t->pos);
	}
	if (p.failed) {
		ast_free(root);
		return -1;
	}
	out->root = root;
	return 0;
}
