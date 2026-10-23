/* Originally based on the UC Irvine CS 238P skeleton by Tony Givargis. */

#include <stdlib.h>
#include <string.h>
#include "front/lexer.h"

static int
is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static int
is_lower(char c)
{
	return c >= 'a' && c <= 'z';
}

static int
is_upper(char c)
{
	return c >= 'A' && c <= 'Z';
}

static int
is_word_start(char c)
{
	return is_lower(c) || is_upper(c) || '_' == c;
}

static int
is_word(char c)
{
	return is_word_start(c) || is_digit(c);
}

static struct token *
push(struct token_list *l, enum tok_kind kind, int pos, int len)
{
	struct token *t;

	if (l->n == l->cap) {
		int cap = l->cap ? 2 * l->cap : 16;
		struct token *v = realloc(l->v, (size_t)cap * sizeof (*v));
		if (!v) {
			return NULL;
		}
		l->v = v;
		l->cap = cap;
	}
	t = &l->v[l->n++];
	memset(t, 0, sizeof (*t));
	t->kind = kind;
	t->pos = pos;
	t->len = len;
	return t;
}

static int
oom(struct token_list *l)
{
	l->err.pos = -1;
	snprintf(l->err.msg, sizeof (l->err.msg), "out of memory");
	return -1;
}

/* Ends the list with a TOK_ERROR token at pos. */
static int
lex_error(struct token_list *l, int pos)
{
	l->err.pos = pos;
	if (!push(l, TOK_ERROR, pos, 0)) {
		return oom(l);
	}
	return 0;
}

/*
 * NUMBER: digits* ['.' digits*] with at least one mantissa digit, then an
 * optional complete exponent [eE][+-]?digits+. A following digit or '.',
 * or no mantissa digit, is a malformed number.
 */
static int
lex_number(struct token_list *l, const char *src, int start, int *next)
{
	struct token *t;
	char *copy;
	int i = start, nd = 0;

	while (is_digit(src[i])) {
		++i;
		++nd;
	}
	if ('.' == src[i]) {
		++i;
		while (is_digit(src[i])) {
			++i;
			++nd;
		}
	}
	if (nd && ('e' == src[i] || 'E' == src[i])) {
		int j = i + 1;
		if ('+' == src[j] || '-' == src[j]) {
			++j;
		}
		if (is_digit(src[j])) {
			while (is_digit(src[j])) {
				++j;
			}
			i = j;
		}
	}
	if (!nd || '.' == src[i] || is_digit(src[i])) {
		snprintf(l->err.msg, sizeof (l->err.msg), "malformed number");
		*next = -1;
		return lex_error(l, start);
	}
	if (!(copy = malloc((size_t)(i - start) + 1))) {
		return oom(l);
	}
	memcpy(copy, src + start, (size_t)(i - start));
	copy[i - start] = '\0';
	if (!(t = push(l, TOK_NUMBER, start, i - start))) {
		free(copy);
		return oom(l);
	}
	t->num = strtod(copy, NULL);
	free(copy);
	*next = i;
	return 0;
}

static int
lex_word(struct token_list *l, const char *src, int start, int *next)
{
	int i = start, upper = 0;

	while (is_word(src[i])) {
		upper |= is_upper(src[i]);
		++i;
	}
	if (upper || i - start > LEX_MAX_NAME) {
		snprintf(l->err.msg, sizeof (l->err.msg), "%s",
			 upper ? "names are lowercase"
			       : "name too long (max 31 characters)");
		*next = -1;
		return lex_error(l, start);
	}
	if (!push(l, TOK_NAME, start, i - start)) {
		return oom(l);
	}
	*next = i;
	return 0;
}

int
lex(const char *src, struct token_list *l)
{
	int i = 0;

	memset(l, 0, sizeof (*l));
	for (;;) {
		char c = src[i];
		struct token *t;
		int next;

		if (' ' == c || '\t' == c || '\n' == c) {
			++i;
			continue;
		}
		if ('\0' == c) {
			if (!push(l, TOK_END, i, 0)) {
				return oom(l);
			}
			return 0;
		}
		if (is_digit(c) || '.' == c) {
			if (lex_number(l, src, i, &next)) {
				return -1;
			}
			if (next < 0) {
				return 0;
			}
			i = next;
			continue;
		}
		if (is_word_start(c)) {
			if (lex_word(l, src, i, &next)) {
				return -1;
			}
			if (next < 0) {
				return 0;
			}
			i = next;
			continue;
		}
		if (strchr("+-*/^", c)) {
			if (!(t = push(l, TOK_OP, i, 1))) {
				return oom(l);
			}
			t->op = c;
		}
		else if ('(' == c || ')' == c || ',' == c) {
			enum tok_kind k = '(' == c ? TOK_LPAREN
					: ')' == c ? TOK_RPAREN : TOK_COMMA;
			if (!push(l, k, i, 1)) {
				return oom(l);
			}
		}
		else {
			snprintf(l->err.msg, sizeof (l->err.msg),
				 "unexpected character '%c'", c);
			return lex_error(l, i);
		}
		++i;
	}
}

void
token_list_free(struct token_list *l)
{
	if (l) {
		free(l->v);
		memset(l, 0, sizeof (*l));
	}
}

const char *
token_kind_name(enum tok_kind k)
{
	switch (k) {
	case TOK_NUMBER: return "number";
	case TOK_NAME:   return "name";
	case TOK_OP:     return "op";
	case TOK_LPAREN: return "lparen";
	case TOK_RPAREN: return "rparen";
	case TOK_COMMA:  return "comma";
	case TOK_END:    return "end";
	case TOK_ERROR:  return "error";
	}
	return "?";
}

void
token_list_print(const struct token_list *l, const char *src, FILE *out)
{
	int i;

	for (i = 0; i < l->n; ++i) {
		const struct token *t = &l->v[i];
		if (TOK_END == t->kind || TOK_ERROR == t->kind) {
			fprintf(out, "%d %s\n", t->pos, token_kind_name(t->kind));
		}
		else {
			fprintf(out, "%d %s %.*s\n", t->pos,
				token_kind_name(t->kind), t->len, src + t->pos);
		}
	}
}
