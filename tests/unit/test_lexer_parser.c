/* Characterization tests for the lexer/parser (current behaviour). */

#include "../check.h"
#include "parser.h"
#include <stdlib.h>

/* Parse s; returns parser (caller closes) or NULL. */
static struct parser *
open_ok(const char *s)
{
	struct parser *p = parser_open(s);
	CHECK(p != NULL);
	return p;
}

static int
is_val(const struct parser_dag *d, double v)
{
	return d && PARSER_DAG_VAL == d->op && v == d->val &&
	       !d->left && !d->right;
}

static void
test_precedence(void)
{
	/* 1+2*3 => ADD(1, MUL(2,3)) */
	struct parser *p = open_ok("1+2*3");
	const struct parser_dag *d;
	if (!p) return;
	d = parser_dag(p);
	CHECK(d && PARSER_DAG_ADD == d->op);
	CHECK(is_val(d->left, 1.0));
	CHECK(d->right && PARSER_DAG_MUL == d->right->op);
	CHECK(is_val(d->right->left, 2.0));
	CHECK(is_val(d->right->right, 3.0));
	parser_close(p);

	/* 2*3+1 => ADD(MUL(2,3), 1) */
	p = open_ok("2*3+1");
	if (!p) return;
	d = parser_dag(p);
	CHECK(d && PARSER_DAG_ADD == d->op);
	CHECK(d->left && PARSER_DAG_MUL == d->left->op);
	CHECK(is_val(d->right, 1.0));
	parser_close(p);
}

static void
test_left_assoc(void)
{
	/* 8-3-2 => SUB(SUB(8,3), 2) */
	struct parser *p = open_ok("8-3-2");
	const struct parser_dag *d;
	if (!p) return;
	d = parser_dag(p);
	CHECK(d && PARSER_DAG_SUB == d->op);
	CHECK(d->left && PARSER_DAG_SUB == d->left->op);
	CHECK(is_val(d->left->left, 8.0));
	CHECK(is_val(d->left->right, 3.0));
	CHECK(is_val(d->right, 2.0));
	parser_close(p);

	/* 8/4/2 => DIV(DIV(8,4), 2) */
	p = open_ok("8/4/2");
	if (!p) return;
	d = parser_dag(p);
	CHECK(d && PARSER_DAG_DIV == d->op);
	CHECK(d->left && PARSER_DAG_DIV == d->left->op);
	CHECK(is_val(d->left->left, 8.0));
	CHECK(is_val(d->left->right, 4.0));
	CHECK(is_val(d->right, 2.0));
	parser_close(p);
}

static void
test_unary(void)
{
	/* -3 => NEG(3), right child only */
	struct parser *p = open_ok("-3");
	const struct parser_dag *d;
	if (!p) return;
	d = parser_dag(p);
	CHECK(d && PARSER_DAG_NEG == d->op);
	CHECK(d && !d->left);
	CHECK(d && is_val(d->right, 3.0));
	parser_close(p);

	/* --3 => NEG(NEG(3)) */
	p = open_ok("--3");
	if (!p) return;
	d = parser_dag(p);
	CHECK(d && PARSER_DAG_NEG == d->op);
	CHECK(d && d->right && PARSER_DAG_NEG == d->right->op);
	CHECK(d && d->right && is_val(d->right->right, 3.0));
	parser_close(p);

	/* +5 => plain VAL(5) (unary plus emits no node) */
	p = open_ok("+5");
	if (!p) return;
	CHECK(is_val(parser_dag(p), 5.0));
	parser_close(p);
}

static void
test_parens(void)
{
	/* (1+2)*3 => MUL(ADD(1,2), 3) */
	struct parser *p = open_ok("(1+2)*3");
	const struct parser_dag *d;
	if (!p) return;
	d = parser_dag(p);
	CHECK(d && PARSER_DAG_MUL == d->op);
	CHECK(d->left && PARSER_DAG_ADD == d->left->op);
	CHECK(is_val(d->left->left, 1.0));
	CHECK(is_val(d->left->right, 2.0));
	CHECK(is_val(d->right, 3.0));
	parser_close(p);

	/* ((4)) => VAL(4) */
	p = open_ok("((4))");
	if (!p) return;
	CHECK(is_val(parser_dag(p), 4.0));
	parser_close(p);
}

static void
test_ids_unique(void)
{
	struct parser *p = open_ok("1+2*3");
	const struct parser_dag *d;
	if (!p) return;
	d = parser_dag(p);
	CHECK(d->id != d->left->id);
	CHECK(d->id != d->right->id);
	CHECK(d->left->id != d->right->id);
	CHECK(d->right->left->id != d->right->right->id);
	parser_close(p);
}

static void
test_rejections(void)
{
	/* parse errors print "error:" lines on stderr; that is expected */
	CHECK(NULL == parser_open("(1"));
	CHECK(NULL == parser_open("1)"));
	CHECK(NULL == parser_open("*2"));
	CHECK(NULL == parser_open("abc"));
	/* known bug, fixed in C2: "1+" indexes TBL out of bounds in
	   expr_additive_ (TBL[ADD - MUL] = TBL[2]); skipped, not asserted.
	   CHECK(NULL == parser_open("1+")); */
}

int
main(void)
{
	test_precedence();
	test_left_assoc();
	test_unary();
	test_parens();
	test_ids_unique();
	test_rejections();
	return CHECK_REPORT("test_lexer_parser");
}
