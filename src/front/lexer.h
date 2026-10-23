/* Originally based on the UC Irvine CS 238P skeleton by Tony Givargis. */

#ifndef JIT_FRONT_LEXER_H
#define JIT_FRONT_LEXER_H

#include <stdio.h>
#include "jit.h"

#define LEX_MAX_NAME 31

enum tok_kind {
	TOK_NUMBER,
	TOK_NAME,
	TOK_OP,     /* + - * / ^ (op holds the character) */
	TOK_LPAREN,
	TOK_RPAREN,
	TOK_COMMA,
	TOK_END,
	TOK_ERROR   /* lexical error at pos; the message is in token_list.err */
};

struct token {
	enum tok_kind kind;
	int pos;        /* byte offset of the first character */
	int len;        /* length of the source slice */
	char op;        /* TOK_OP only */
	double num;     /* TOK_NUMBER only */
};

struct token_list {
	struct token *v;
	int n, cap;
	struct jit_error err; /* valid when the last token is TOK_ERROR */
};

/*
 * Tokenizes src. The list always ends with TOK_END or, on a lexical error,
 * TOK_ERROR (err filled). Returns 0, or -1 on out of memory (err filled).
 */
int lex(const char *src, struct token_list *out);

void token_list_free(struct token_list *);

const char *token_kind_name(enum tok_kind);

/* "<pos> <kind> <text>" per token; end prints "<pos> end". */
void token_list_print(const struct token_list *, const char *src, FILE *out);

#endif /* JIT_FRONT_LEXER_H */
