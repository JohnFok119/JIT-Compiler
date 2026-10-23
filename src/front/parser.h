/* Originally based on the UC Irvine CS 238P skeleton by Tony Givargis. */

#ifndef JIT_FRONT_PARSER_H
#define JIT_FRONT_PARSER_H

#include "jit.h"
#include "front/ast.h"
#include "front/lexer.h"

struct parse_result {
	struct ast *root;
	int nvars;
	char vars[JIT_MAX_VARS][32];  /* in order of first appearance */
};

/*
 * Parses the token list of src (from lex()). legacy != 0 accepts only
 * numbers, + - * / and parentheses. Returns 0, or -1 with err filled.
 */
int parse(const char *src, const struct token_list *toks, int legacy,
	  struct parse_result *out, struct jit_error *err);

#endif /* JIT_FRONT_PARSER_H */
