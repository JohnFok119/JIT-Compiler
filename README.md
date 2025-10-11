# JIT Compiled Expression Evaluator

## Overview

Design a program that accepts a mathematical expression (a string containing the symbols: +, -, *, /, (, ), value). This program will dynamically generate a C program based on the input string, invoke a C compiler to create an equivalent loadable module, and then load and execute the machine code equivalent of the C program to produce the evaluated result of the expression.

## Extra Credit (implemented)

sigmoid(double x) function that resides in your main program but is called from the generated C code to transform the final value of the expression (i.e., sigmoid(expression-value)).

## Testing

Used printf() functions to track the location of the child/parent processes.
