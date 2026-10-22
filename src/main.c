/**
 * Tony Givargis
 * Copyright (C), 2023-2025
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * main.c
 */

#include "jitc.h"
#include "parser.h"
#include "system.h"
#include <math.h>


static void
reflect(const struct parser_dag *dag, FILE *file)
{
	if (dag) {
		reflect(dag->left, file);
		reflect(dag->right, file);
		if (PARSER_DAG_VAL == dag->op) {
			fprintf(file,
				"double t%d = %f;\n",
				dag->id,
				dag->val);
		}
		else if (PARSER_DAG_NEG == dag->op) {
			fprintf(file,
				"double t%d = - t%d;\n",
				dag->id,
				dag->right->id);
		}
		else if (PARSER_DAG_MUL == dag->op) {
			fprintf(file,
				"double t%d = t%d * t%d;\n",
				dag->id,
				dag->left->id,
				dag->right->id);
		}
		else if (PARSER_DAG_DIV == dag->op) {
			fprintf(file,
				"double t%d = t%d ? (t%d / t%d) : 0.0;\n",
				dag->id,
				dag->right->id,
				dag->left->id,
				dag->right->id);
		}
		else if (PARSER_DAG_ADD == dag->op) {
			fprintf(file,
				"double t%d = t%d + t%d;\n",
				dag->id,
				dag->left->id,
				dag->right->id);
		}
		else if (PARSER_DAG_SUB == dag->op) {
			fprintf(file,
				"double t%d = t%d - t%d;\n",
				dag->id,
				dag->left->id,
				dag->right->id);
		}
		else {
			EXIT("software");
		}
	}
}

double
sigmoid(double x)
{
	return 1.0 / (1.0 + exp(-1*x));
}
/* generate actually writes onto out.c*/
/* reflect does the math calculation*/

static void
generate(const struct parser_dag *dag, FILE *file)
{
	fprintf(file, "double evaluate(void) {\n");
	reflect(dag, file);
	fprintf(file, "double result = t%d;\n", dag->id);
	/* fprintf(file, "double result = t%d;\n", dag->id); */
	fprintf(file, "double (*sigmoidptr)(double) = (double (*)(double))%lu;\n", (unsigned long)sigmoid);
	/*
		&sigmoid = (double) (*)(double) = pointer to a function that takes a double and returns a double
		- We need to cast the sigmoidptr to that type  with printf formatting
		- Since printf does not support function pointer types we need to convert the sigmoid to a type that printf can take without losing its address
		- Once (unsigned long)sigmoid, we pass it to printf to be converted to the corresponding type
		- sigmoidptr is now a ((double) (*)(double)) and can be called upon
	*/
	fprintf(file, "return sigmoidptr(result);\n");
	/*fprintf(file, "return result);*/
	fprintf(file, "}\n");
}


typedef double (*evaluate_t)(void);

int
main(int argc, char *argv[])
{
	const char *SOFILE = "./out.so";
	const char *CFILE = "out.c";
	struct parser *parser;
	struct jitc *jitc;
	evaluate_t fnc;
	FILE *file;

	/* usage */

	if (2 != argc) {
		printf("usage: %s expression\n", argv[0]);
		return -1;
	}

	/* parse */

	if (!(parser = parser_open(argv[1]))) {
		TRACE(0);
		return -1;
	}

	/* generate C */

	if (!(file = fopen(CFILE, "w"))) {
		TRACE("fopen()");
		return -1;
	}
	generate(parser_dag(parser), file);
	parser_close(parser);
	fclose(file);

	/* JIT compile */

	if (jitc_compile(CFILE, SOFILE)) {
		file_delete(CFILE);
		TRACE(0);
		return -1;
	}
	file_delete(CFILE);

	/* dynamic load */

	if (!(jitc = jitc_open(SOFILE)) ||
	    !(fnc = (evaluate_t)jitc_lookup(jitc, "evaluate"))) {
		file_delete(SOFILE);
		jitc_close(jitc);
		TRACE(0);
		return -1;
	}
	printf("%f\n", fnc());

	/* done */

	file_delete(SOFILE);
	jitc_close(jitc);
	return 0;
}
